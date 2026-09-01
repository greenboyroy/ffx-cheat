/*
 * ffx_cheat.c
 *
 * Native Linux port of the `cheat.ffx` subset of Kaldaien's UnX mod
 * (https://github.com/Kaldaien/UnX), targeting FINAL FANTASY X HD Remaster
 * running under Wine/Proton.
 *
 * Ported:
 *   - entire_party_earns_ap
 *   - permanent_sensor
 *
 * Deliberately NOT ported:
 *   - the speed-hack / dialogue-skip feature. In the original it works by
 *     inline-hooking the game's tick function (FFX_GameTick) via Special K's
 *     MinHook wrapper, multiplying the incoming delta-time argument. That
 *     needs a real code detour (Frida or a hand-rolled ptrace trampoline) --
 *     out of scope here by request.
 *
 * How this differs architecturally from the original:
 *   The original is an in-process injected DLL (unx.dll) riding alongside a
 *   second injected DLL (Special K's dxgi.dll) that supplies the hooking
 *   engine. This tool is instead an *external*, unprivileged-except-for-
 *   ptrace process: it never loads code into the game, it just reads/writes
 *   specific bytes in the target's address space from outside, the same way
 *   a debugger or Cheat Engine does. No DLL versioning, no ABI between two
 *   binaries to keep in sync -- which was the recurring source of breakage
 *   in UnX's own changelog.
 *
 * Offsets: taken verbatim from UnX/UnX/cheat.cpp's inline comments (they are
 * RVAs relative to ffx.exe's load base). The one non-literal number is
 * PARTY_STRIDE, hand-derived from the party_s struct layout in that file --
 * see the comment above its definition. Run with --dump and sanity-check the
 * output before trusting this tool to write anything.
 *
 * Permissions: reading/writing another process's memory needs ptrace access.
 * Either run this as root, or for your user session:
 *     sudo sysctl kernel.yama.ptrace_scope=0
 *
 * Build:
 *     gcc -O2 -Wall -o ffx_cheat ffx_cheat.c
 *
 * Usage:
 *     ./ffx_cheat --dump                          # sanity-check offsets first
 *     ./ffx_cheat --ap --sensor                    # run both cheats
 *     ./ffx_cheat --pid 12345 --ap                 # skip auto-detection
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <time.h>
#include <inttypes.h>

/* ---------------------------------------------------------------------- *
 * Offsets reverse-engineered by the UnX project. RVAs relative to the
 * ffx.exe module's load base.
 * ---------------------------------------------------------------------- */
#define OFF_DEBUG_FLAGS  0x0D2A8F8UL  /* unx_ffx_memory_s::offsets::Debug     */
#define OFF_PARTY_BASE   0x0D32060UL  /* unx_ffx_memory_s::offsets::PartyBase */
#define OFF_IN_BATTLE    0x1F10EA0UL  /* unx_ffx_memory_s::offsets::InBattle  */
#define OFF_GAINED_AP    0x1F10EC4UL  /* unx_ffx_memory_s::offsets::GainedAp  */

/* permanent_sensor sits at absolute 0x0D2A915 in cheat.cpp's comments;
 * 0x0D2A915 - 0x0D2A8F8 = 0x1D bytes into the debug_flags struct. */
#define OFF_PERMANENT_SENSOR (OFF_DEBUG_FLAGS + 0x1DUL)

/* party_s per-character stride and the in_party field's offset within it.
 * Only used now to decide who's "active" for the AP cheat (in_party is read,
 * never written, since playable_seymour was dropped). NOT given directly as
 * an absolute address in cheat.cpp -- derived by hand from the party_s field
 * list (base/AP/vitals/in_party/...), assuming default (unpacked) MSVC
 * struct alignment:
 *   base   : 4+4+8*1           = 16  bytes  -> in_party's struct starts at 0
 *   AP     : 4+4                = 8   bytes  -> offset 16
 *   vitals : 4+4+4+4            = 16  bytes  -> offset 24
 *   in_party (u8)                          -> offset 40  (0x28)
 *   ... remaining fields, with one 2-byte alignment pad before skill_flags ...
 *   total struct size                      -> 148 bytes (0x94)
 * VERIFY WITH --dump BEFORE TRUSTING THE AP CHEAT.
 */
#define PARTY_STRIDE   0x94U   /* 148 */
#define IN_PARTY_OFF   0x28U   /* 40  */
#define AP_TOTAL_OFF   0x10U   /* 16  */
#define AP_CURRENT_OFF 0x14U   /* 20  */

#define CHAR_COUNT     7   /* Tidus..Rikku -- matches original's AP loop bound */

static const char *kCharNames[8] = {
  "Tidus", "Yuna", "Auron", "Kimahri", "Wakka", "Lulu", "Rikku", "Seymour"
};

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* ---------------------------------------------------------------------- *
 * Process memory I/O
 * ---------------------------------------------------------------------- */

static bool pv_read(pid_t pid, uint64_t addr, void *buf, size_t len) {
  struct iovec local  = { buf, len };
  struct iovec remote = { (void *)(uintptr_t)addr, len };
  return process_vm_readv(pid, &local, 1, &remote, 1, 0) == (ssize_t)len;
}

static bool pv_write(pid_t pid, uint64_t addr, const void *buf, size_t len) {
  struct iovec local  = { (void *)buf, len };
  struct iovec remote = { (void *)(uintptr_t)addr, len };
  return process_vm_writev(pid, &local, 1, &remote, 1, 0) == (ssize_t)len;
}

static bool write_u8(pid_t pid, uint64_t addr, uint8_t val) {
  return pv_write(pid, addr, &val, 1);
}

static bool read_u8(pid_t pid, uint64_t addr, uint8_t *val) {
  return pv_read(pid, addr, val, 1);
}

/* ---------------------------------------------------------------------- *
 * Process / module discovery
 * ---------------------------------------------------------------------- */

static pid_t find_pid_by_exe(const char *needle) {
  DIR *d = opendir("/proc");
  if (!d) return -1;

  struct dirent *ent;
  pid_t found = -1;

  while ((ent = readdir(d)) != NULL) {
    if (!isdigit((unsigned char)ent->d_name[0])) continue;

    char path[300];
    snprintf(path, sizeof path, "/proc/%s/cmdline", ent->d_name);

    FILE *f = fopen(path, "rb");
    if (!f) continue;

    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;

    /* cmdline args are NUL-separated; flatten to spaces for a simple
     * substring search across all of argv. */
    for (size_t i = 0; i < n; i++) if (buf[i] == 0) buf[i] = ' ';

    if (strcasestr(buf, needle)) {
      found = (pid_t)atoi(ent->d_name);
      break;
    }
  }

  closedir(d);
  return found;
}

static uint64_t find_module_base(pid_t pid, const char *needle) {
  char path[64];
  snprintf(path, sizeof path, "/proc/%d/maps", pid);

  FILE *f = fopen(path, "r");
  if (!f) return 0;

  char line[1024];
  uint64_t base = 0;
  bool have_base = false;

  while (fgets(line, sizeof line, f)) {
    if (!strcasestr(line, needle)) continue;

    uint64_t start;
    if (sscanf(line, "%" SCNx64 "-", &start) != 1) continue;

    if (!have_base || start < base) {
      base = start;
      have_base = true;
    }
  }

  fclose(f);
  return have_base ? base : 0;
}

/* ---------------------------------------------------------------------- *
 * Cheat application -- mirrors UnX's cheat.cpp :: CheatTimer_FFX
 * ---------------------------------------------------------------------- */

typedef struct {
  bool ap;
  bool sensor;
} cheat_state_t;

static void apply_cheats(pid_t pid, uint64_t base, const cheat_state_t *want) {
  static bool warned_write_failure = false;

  /* --- permanent sensor --- */
  if (!write_u8(pid, base + OFF_PERMANENT_SENSOR, want->sensor ? 1 : 0)) {
    if (!warned_write_failure) {
      fprintf(stderr, "\n[warn] write to permanent_sensor failed -- the page "
                       "may be read-only under Wine. A ptrace(PTRACE_POKEDATA) "
                       "forced-write fallback would be needed here.\n");
      warned_write_failure = true;
    }
  }

  /* --- whole party earns AP --- */
  if (want->ap) {
    bool active[CHAR_COUNT];

    for (int i = 0; i < CHAR_COUNT; i++) {
      uint64_t in_party_addr = base + OFF_PARTY_BASE
                                     + (uint64_t)i * PARTY_STRIDE
                                     + IN_PARTY_OFF;
      uint8_t state = 0;
      read_u8(pid, in_party_addr, &state);
      active[i] = (state != 0x00 && state != 0x10);
    }

    for (int i = 0; i < CHAR_COUNT; i++) {
      uint64_t participation_addr = base + OFF_IN_BATTLE + (uint64_t)i;

      if (active[i]) {
        uint8_t cur = 0;
        read_u8(pid, participation_addr, &cur);
        if (cur != 1) write_u8(pid, participation_addr, 2);
      } else {
        write_u8(pid, participation_addr, 0);
      }
    }

    for (int i = 0; i < CHAR_COUNT; i++) {
      uint64_t ap_addr = base + OFF_GAINED_AP + (uint64_t)i;
      write_u8(pid, ap_addr, active[i] ? 1 : 0);
    }
  }
}

/* ---------------------------------------------------------------------- *
 * Diagnostic dump -- run this BEFORE trusting the tool with writes.
 * ---------------------------------------------------------------------- */

static void dump_state(pid_t pid, uint64_t base) {
  printf("Base address: 0x%" PRIx64 "\n\n", base);

  uint8_t sensor = 0;
  if (read_u8(pid, base + OFF_PERMANENT_SENSOR, &sensor))
    printf("permanent_sensor byte : 0x%02x\n\n", sensor);
  else
    printf("permanent_sensor byte : <read failed>\n\n");

  printf("Party table (expect a handful of distinct small byte values for\n"
         "in_party, not random garbage -- that's your smoke test for whether\n"
         "PARTY_STRIDE is correct):\n\n");
  printf("%-8s  in_party  AP.total  AP.current\n", "Char");

  for (int i = 0; i < 8; i++) {
    uint64_t p = base + OFF_PARTY_BASE + (uint64_t)i * PARTY_STRIDE;

    uint8_t in_party = 0;
    read_u8(pid, p + IN_PARTY_OFF, &in_party);

    uint32_t ap_total = 0, ap_current = 0;
    pv_read(pid, p + AP_TOTAL_OFF,   &ap_total,   4);
    pv_read(pid, p + AP_CURRENT_OFF, &ap_current, 4);

    printf("%-8s  0x%02x      %-8u  %-8u\n",
           kCharNames[i], in_party, ap_total, ap_current);
  }
}

/* ---------------------------------------------------------------------- *
 * main
 * ---------------------------------------------------------------------- */

static void usage(const char *argv0) {
  fprintf(stderr,
    "Usage: %s [--pid PID] [--name NEEDLE] [--dump] [--ap] [--sensor]\n"
    "           [--wait] [--wait-timeout SECONDS] [--interval-ms N]\n"
    "  --pid PID              attach to this PID instead of auto-detecting\n"
    "  --name NEEDLE          substring to match in cmdline (default: ffx.exe;\n"
    "                         use ffx-2.exe for the sequel)\n"
    "  --dump                 print current values and exit -- no writes\n"
    "  --ap                   enable whole-party-earns-AP\n"
    "  --sensor               enable permanent sensor\n"
    "  --wait                 poll until the process/module appears instead of\n"
    "                         failing immediately -- needed when launched\n"
    "                         before the game itself (e.g. from Steam launch\n"
    "                         options), since Proton startup time varies\n"
    "  --wait-timeout SECONDS give up waiting after this long (default 120)\n"
    "  --interval-ms N        poll interval, ms (default 33, matches original)\n",
    argv0);
}

int main(int argc, char **argv) {
  pid_t pid = -1;
  const char *needle = "ffx.exe";
  bool dump_only = false;
  cheat_state_t want = { 0 };
  int interval_ms = 33;
  bool wait_for_process = false;
  int wait_timeout_s = 120;

  for (int i = 1; i < argc; i++) {
    if      (!strcmp(argv[i], "--pid")         && i + 1 < argc) pid = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--name")        && i + 1 < argc) needle = argv[++i];
    else if (!strcmp(argv[i], "--dump"))       dump_only = true;
    else if (!strcmp(argv[i], "--ap"))         want.ap = true;
    else if (!strcmp(argv[i], "--sensor"))     want.sensor = true;
    else if (!strcmp(argv[i], "--wait"))       wait_for_process = true;
    else if (!strcmp(argv[i], "--wait-timeout") && i + 1 < argc) wait_timeout_s = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--interval-ms") && i + 1 < argc) interval_ms = atoi(argv[++i]);
    else { usage(argv[0]); return 1; }
  }

  signal(SIGINT, on_sigint);

  if (pid < 0) {
    if (wait_for_process)
      printf("Waiting for a process matching \"%s\" (timeout %ds)...\n",
             needle, wait_timeout_s);

    time_t deadline = time(NULL) + wait_timeout_s;

    while ((pid = find_pid_by_exe(needle)) < 0) {
      if (!wait_for_process) {
        fprintf(stderr, "Could not find a running process matching \"%s\".\n"
                         "Pass --pid explicitly, or pass --wait to poll until "
                         "it starts.\n", needle);
        return 1;
      }
      if (g_stop) {
        fprintf(stderr, "Interrupted while waiting for the process.\n");
        return 1;
      }
      if (time(NULL) >= deadline) {
        fprintf(stderr, "Timed out after %ds waiting for \"%s\" to appear.\n",
                wait_timeout_s, needle);
        return 1;
      }
      sleep(1);
    }
    printf("Found process: PID %d\n", pid);
  }

  uint64_t base = 0;
  {
    time_t deadline = time(NULL) + wait_timeout_s;

    while ((base = find_module_base(pid, needle)) == 0) {
      if (!wait_for_process) {
        fprintf(stderr,
          "Could not find a memory mapping matching \"%s\" in PID %d.\n"
          "Either the PID is wrong, or this is a permission problem reading\n"
          "/proc/%d/maps -- try running as root, or:\n"
          "  sudo sysctl kernel.yama.ptrace_scope=0\n", needle, pid, pid);
        return 1;
      }
      if (g_stop) {
        fprintf(stderr, "Interrupted while waiting for the module to load.\n");
        return 1;
      }
      if (time(NULL) >= deadline) {
        fprintf(stderr, "Timed out after %ds waiting for \"%s\" to map into "
                         "PID %d.\n", wait_timeout_s, needle, pid);
        return 1;
      }
      sleep(1);
    }
  }

  if (dump_only) {
    dump_state(pid, base);
    return 0;
  }

  /* Permission smoke test before entering the loop. */
  uint8_t probe;
  if (!read_u8(pid, base + OFF_PERMANENT_SENSOR, &probe)) {
    fprintf(stderr,
      "Failed to read target memory (errno=%d: %s).\n"
      "You likely need root, or a looser ptrace_scope:\n"
      "  sudo sysctl kernel.yama.ptrace_scope=0\n", errno, strerror(errno));
    return 1;
  }

  printf("Base 0x%" PRIx64 " -- AP:%s Sensor:%s -- interval %dms\n",
         base, want.ap ? "on" : "off",
         want.sensor ? "on" : "off", interval_ms);
  printf("Press Ctrl+C to stop.\n");

  struct timespec ts = { interval_ms / 1000, (long)(interval_ms % 1000) * 1000000L };

  while (!g_stop) {
    apply_cheats(pid, base, &want);

    if (kill(pid, 0) != 0) {
      printf("\nTarget process exited.\n");
      break;
    }

    nanosleep(&ts, NULL);
  }

  return 0;
}
