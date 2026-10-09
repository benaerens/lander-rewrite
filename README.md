# Lander rewrite

A C reimplementation of Psygnosis' **Lander** (1999, Windows), written from scratch by reverse
engineering the original program. It plays the way the original does: the intro films, the front end
with the mail, the shop and saved games, the campaign, the training and deathmatch missions and the
CD soundtrack. It runs on today's Windows, full screen, with widescreen missions.

**This download does not include the game.** You need your own Lander CD, or an image of it. The
first time you start the game, it asks for your disc image and copies the game files from it.

<img width="49%" height="49%" alt="Screenshot 2026-10-10 011323" src="https://github.com/user-attachments/assets/608cdf02-fc04-428f-bfa2-8c8d2442cb7f" />
<img width="49%" height="49%" alt="Screenshot 2026-10-10 011514" src="https://github.com/user-attachments/assets/048b2992-eaeb-48a4-86bf-fa18bc6d91cb" />


## Playing (Windows)

1. Download `lander-rewrite-windows-x64.zip` from [Releases](../../releases) and unzip it anywhere.
   You get this folder:
   ```
   Lander rewrite\
      Lander.exe                          <- double-click to play
      Put your Lander disc image here\    <- your disc image goes in here
      How to play.txt
   ```
2. Put your Lander disc image into *Put your Lander disc image here*.
   - **Supported images:**
     - `.cue` + `.bin`, which includes the music
     - `.mds` + `.mdf` (Alcohol 120%), which includes the music
     - `.iso` and `.img`, which have no music: Lander's soundtrack is CD audio, and an .iso only
       holds the data track
   - A copy of the CD's files works too (without the music). Unpack a `.zip` first.
3. Double-click `Lander.exe`.
   - **The first start** copies the game files from the image to `%APPDATA%\LanderRewrite\data`,
     about 680 MB. This takes a minute and happens once; newer versions of the download find these
     files by themselves. After that the image isn't needed any more.
   - **Other ways to give the image:** if the folder is empty, the game asks for the image, and you
     can drag it onto the window. You can also drop the image file onto `Lander.exe`, or run
     `Lander.exe --image "D:\Games\Lander.cue"`.

If you have the physical CD, make an image of it first with a CD imaging program. Choose BIN/CUE so
that the music tracks are included.

Windows SmartScreen may warn about the download, because the program isn't signed. Click
**More info**, then **Run anyway**.

## Settings

| | |
|---|---|
| Controls | the original's control sets (keyboard, mouse, joystick/game pad) and its key editor: *Options → Controls* |
| Full screen / window | **Alt+Enter**, or start with `--windowed` |
| Widescreen | Missions use your monitor's shape. The view is the original's vertical view, with more visible at the sides. Menus and films stay 4:3. Use `--aspect 4:3` for the original's shape. |
| Sharper 3D | `--render-height 960` (480 is the original's look). |
| Saved games, settings | `%APPDATA%\LanderRewrite`. `SAVEGAME.DAT` uses the original's format. `LANDER.CFG` holds the settings, including `Aspect` and `Render Height`. |
| Skip the intro films | `--skip-intro` |
| Use a folder of game files | `--data <folder>`, for a folder holding the CD's files |
| Play one mission | `--mission TRAINING/1` (world/number) |

Not there yet: network multiplayer (deathmatch maps can be played alone).

## Building

**Windows:** install [zig](https://ziglang.org/download/) (0.17.0), then run:

```bash
powershell -ExecutionPolicy Bypass -File build-windows.ps1
```

This gives `build\lander.exe` (the game) and `build\lander-viewer.exe` (a free-flying level viewer).
`tools\package-windows.ps1` then makes the players' download: the folder `build\package\Lander rewrite`
and `build\lander-rewrite-windows-x64.zip`. A pushed tag `v*` does the same on GitHub and attaches the
zip to a release.

**Linux / WSL:** run `make`. This builds the test programs:

- `build/lander_headless`: the whole game without a window, with scripted input and PNG frames.
- `build/disc_extract`: copies the game files from a disc image, like the game's first start.

There is no Linux window/audio layer yet. A port implements [`src/platform.h`](src/platform.h); see
`src/platform_win32.c` and `src/platform_headless.c`.

## How it's made

The original `lander.EXE` and its mission DLLs were taken apart with Ghidra and rebuilt in portable
C11, function by function, with the original addresses noted in the comments. The rebuilt parts:

- the PSX-style fixed-point maths, task scheduler and event system
- the objects, physics and collision
- the 33 mission scripts
- the front end's windows and controls
- the 3D sound rules

The renderer is a new software renderer. [`NOTES.md`](NOTES.md) has the technical notes. The project
follows the idea of [wipeout-rewrite](https://github.com/phoboslab/wipeout-rewrite).

## Legal

Lander is © 1999 Psygnosis Ltd (now part of Sony Interactive Entertainment). This project is not
affiliated with or endorsed by Sony Interactive Entertainment or Psygnosis. It contains only new
source code. It doesn't include or distribute the original game's files, program, music, graphics or
text: you need your own copy of the game. Please don't post game files, or links to them, in issues
or pull requests.

The code in this repository is under the MIT license ([LICENSE](LICENSE)). It includes
[pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski (MIT); see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
