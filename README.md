# Final Fantasy X Party AP and Sensor for Steam Deck players

## About

Building off the excellent work of ["Untitled" Project X](https://github.com/Kaldaien/UnX), a 10 year old project that I could not get to run on my Steam Deck without persistent crashing (because it was indeed created for a Windows environment long before the Steam Deck existed). I asked Claude to dig into the repo with the sole focus of figuring out how two specific cheats worked, namely Party AP and Sensor.

Turns out they are simply memory pokes so they could be ported to run natively to a Linux environment, like Steam OS. So, that's what I asked Claude to do next and this is the result. I've been playing with this for a good few hours on the Blitz Ace grind and not had one crash yet, your mileage may vary, I never sleep my deck mid-game so that might need testing for you...

**Please read the setup instructions and the disclaimer fully before trying to install! Yes, it seems like there is a lot of words but I'm trying to make it useful for the less nerdy among us.**

## Setup (this is a WIP, I need to finalise this)

* From Steam, download FFX HD Remaster and run the game from the launcher, at least once.
* Switch to desktop mode and open [this repo](https://github.com/greenboyroy/ffx-cheat) from a browser (you might need to install Firefox if you haven't already).
* Download the latest release (look right).
* Open Dolphin File Manager (the open folder icon).
* Navigate to the Downloads folder (should be on the left under Places), extract the zip you downloaded, right-click and copy the `ffx_cheat` folder.
* Navigate to the Home folder and paste.
* Open the ffx_cheat folder, you should see an executable and some script files, the scripts just run the executable with the option(s) you want.
* We only need one script file but I gave you options, if you only want AP or Sensor or both, if you want you can delete the ones you don't want or you can just leave them be.
* Next is probably the most scary part, giving the script you want permission to run. Right-click some empty space in the current folder and select 'Open Terminal Here'. This will open a new window with a prompt.
* Enter the command for the script you want to run. You only need **ONE** of these:
  
  `chmod +x ffx_launch_ap_sensor.sh`  
  **OR**  
  `chmod +x ffx_launch_ap_only.sh`  
  **OR**  
  `chmod +x ffx_launch_sensor_only.sh`  
  
* Press enter. If you typed the command in properly, you should just get a new line waiting for another prompt. If you got an error, you probably did something wrong but do a quick Google search to check before trying again.
* OK, you can close the file manager and terminal windows now. Let's open the desktop version of Steam, which should be at the top of the screen.
* Go to the FFX game page in your library and open the game properties. These can be found by clicking the cog icon opposite the Play button.
* Under 'Launch Options' enter the **ONE** of these commands which relates to the script you want to run:
  
  `/home/deck/ffx_cheat/ffx_launch_ap_sensor.sh %command%`  
  **OR**  
  `/home/deck/ffx_cheat/ffx_launch_ap_only.sh %command%`  
  **OR**  
  `/home/deck/ffx_cheat/ffx_launch_sensor_only.sh %command%`  
  
* Close the properties window.
* Click the Play button to test, let it cook for a sec when it's launching. Select FFX from the launcher and play as normal. If everything is working, in battle you should be able to see if you have sensor if you can always see the enemy XP and elemental weaknesses without any sensor abilities. At the end of a battle everyone in the party should get AP, even if you didn't use them.
* You can close your game and return to Gaming Mode and everything should just work for future sessions. Party on!

## Disclaimer

I have no intention of doing any harm to your deck/machine/PC so if you got the code from here, you should be good to go. However, someone with bad intentions would also say that so... just be careful, huh? This is a memory poking program, the premise of which is generally frowned upon from a security point of view. The code in `ffx_cheat.c` is what I compiled so really to be extra sure you're getting what you signed up for, you *should* compile it yourself on a compatible linux environment, though that is difficult to do on a Steam Deck, without some amount of wizardry, I used Ubuntu via WSL on an old laptop for that.

So that being said, use at your own risk, read the instructions, follow them properly and everything should work. I won't have much time (or patience, tbh) to play support on this, it's just a bit of code I asked an AI to write so that I could better enjoy a video game. I don't have any intentions at this moment in time to make add any other features or create anything for FFX-2 but you never know.
