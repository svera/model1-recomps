# Star Wars Arcade's downloads (read by tools/release/games.sh for tools/mac/package.sh,
# tools/win/package.sh and tools/linux/package.sh). Paths are from the repository's root.
TITLE="Star Wars Arcade Recomp"
FILE=StarWarsArcade
PROG=swa-native                   # native code compiled from the player's ROM at startup
MAC_ID=recomp.swa.unofficial
COPYRIGHT="Unofficial fan project. Not affiliated with or endorsed by SEGA or Lucasfilm."
ICNS=assets/games/swa/icon/SWA.icns   # made by tools/games/swa/release/make_icon.py
ICO=assets/games/swa/icon/SWA.ico
PNG=assets/games/swa/icon/icon.png
README_MAC=$R/README.txt
README_WIN=$R/README.txt
README_LINUX=$R/README.txt
ROMS_README=$R/roms-README.txt    # the Linux package's roms/README.txt
