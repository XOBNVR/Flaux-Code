@echo off
rem Double-click this file to play Star Catcher.
rem It runs Flaux inside WSL (Linux on Windows), in the Flaux-Code folder.
wsl bash -lc "cd ~/Flaux-Code && ./flaux examples/games/star_catcher.flx"
pause
