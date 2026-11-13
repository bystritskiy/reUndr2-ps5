reUndr 2: an unofficial native PS5 port of Need for Speed: Underground 2 (Xbox)

This package holds no game files. You need your own copy of the game: the NTSC-U
Xbox release, as a disc image (.iso) you made from your disc or as unpacked files.
A clean dump and a region or media patched one both work; other releases may not.
It is not affiliated with or endorsed by Electronic Arts or Sony.

INSTALL

1. Copy the PPSA99027 folder to /data/homebrew/PPSA99027 and let ShadowMountPlus
   register it, or install it from your store.
2. Give reUndr 2 your game, either way:
   - A disc image: copy the .iso to /data/nfsu2, or to nfsu2 or the top of a USB
     drive (/mnt/usb0 to /mnt/usb7). reUndr 2 unpacks it once into
     /data/nfsu2/game and shows how far it has got. The game needs about 2.5 GB
     free on the console; the .iso can go once it is unpacked.
   - The unpacked disc: copy it to /data/nfsu2/game, or to nfsu2/game on a USB
     drive. The folder must hold default.xbe next to B3 and NFSUNDER.
3. Start reUndr 2. If the game is missing, if the image is damaged, or if there
   is no room for it, a screen says which and closes on Cross.

INTERFACE

The button icons, the controller picture on the loading screens and the menu
font are drawn for the PS5 and live in hd/. They carry none of the disc's
pixels: the disc gives only the size of each one. For the original Xbox artwork,
put NFSU2_HD=0 in the settings file named below.

CONTROLS

Cross/Circle/Square/Triangle = A/B/X/Y, R1/L1 = Black/White, L2/R2 = triggers,
Options = Start, touchpad press = Back, L3/R3 = stick presses. The prompts in
the game show these PS5 buttons.

Split screen: the second player needs a separate logged-in PS5 user or a guest.
Main menu, right, 2 Player Split-Screen; at car select each player presses Options.

PORT SETTINGS

Touchpad + Options in the game opens the port's settings: graphics, motion
blur, vibration, sound, frame rate and a benchmark, with what is new in each
version. L1/R1 switch tabs, Circle closes. The game's own Options are as on
the Xbox.

SOUND

The game's 5.1 mix goes to the PS5 as eight-channel PCM and follows the
speaker setup in the PS5 settings. Port settings, Audio: Multichannel Sound On
also plays music and the front pair from the centre, subwoofer and rear
speakers; Off keeps only the game's own surround mix. Speaker Test plays a
tone from one speaker, or from all of them in turn.
NFSU2_AUDIO_CHANNELS=2 in nfsu2_env.txt brings back stereo.

Settings, saves and logs are in /data/nfsu2/PPSA99027/; the settings themselves
are in nfsu2_env.txt there. Licences are in licenses/.
