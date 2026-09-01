#! /bin/bash
/home/deck/ffx_cheat/ffx_cheat --wait --ap --sensor > /tmp/ffx_cheat.log 2>&1 &
exec "$@"
