# Lander rewrite — reverse-engineering notes

Goal: a portable C reimplementation of Psygnosis' **Lander** (1999, Windows), in the
style of phoboslab/wipeout-rewrite. It loads the original data from the user's own
disc; no game data is ever put in this source tree.

## Folder layout (lander/)

| Path | What |
|---|---|
| `disc/` | the disc image the work was done with (MDF/MDS, raw 2448-byte sectors; never in this repo) |
| `tracks/` | `Track01.iso` (the raw data track) |
| `cd/` | **the data folder the rewrite reads**: the CD's files + `MUSIC/TRACK02..06.WAV` (CD audio, 44.1 kHz stereo). Same layout as `tools/prepare_data.py` produces (verified identical) |
| `tools/` | Ghidra 12.1.4 + Temurin JDK 21 (downloaded with the user's OK, 2026-10-08) |
| `re/` | reverse-engineering workspace: Ghidra project, scripts, decompiled output |
| `rewrite/` | the new source code (this file lives here) |

Scratch tools used so far (re-creatable): `mdf_extract.py` (MDS parse + split),
`iso_extract.py` (ISO9660/Joliet), `ppm.py`, `tex.py`, `pe_info.py`, `iat.py`.

## The rewrite (state)

* Game data: the game installs it itself on first start from the player's disc image (`src/discimage.c`:
  .mds/.mdf, .cue/.bin, .iso, .img; ISO 9660 + Joliet; CD audio -> `MUSIC/TRACKnn.WAV`) into
  `<save folder>/data`, with `INSTALLED.TXT` written last (a stopped copy is not used). Search order in
  main.c `find_data`: `--data`, the installed copy, the download's disc folder next to the program
  ("Put your Lander disc image here", scanned two folders deep: the CD's files are used in place, the
  best image — .cue, .mds, .iso, .img, .bin, .mdf — is installed, a lone .zip/.rar/.7z asks to unpack),
  `data`/`cd` next to the exe or the working folder (development), `--image` / an image given as
  argument, else the platform's first-start screen (`platform_ask_disc_image`: drag & drop of an image
  or a folder, file dialog, F opens the disc folder). `build/disc_extract` is the same as a command.
  `tools/prepare_data.py <LANDER.mds|.cue|cd folder> <out>` is the older Python form.
* The download (`tools/package-windows.ps1`): `Lander rewrite\` with `Lander.exe` (own icon:
  `src/win/lander.rc` + `lander.ico` from `tools/make_icon.py`; the window takes the original's icon
  from the player's lander.EXE), the disc folder, `How to play.txt`, licences; zipped with '/' names.
* No game text in the source: `src/pe.c` (32-bit PE loader by VA through fs_load) and
  `src/game/gametext.c` fill the string arrays at run time from the player's files. lander.EXE's text
  at start-up, a mission's MISSIONR.DLL text before LevelLoadLevel, brief.dll in FE_LoadBriefDll. Each
  module lists its tables (kind, VA, count, array; the Glide exe's VA for exe text). Addresses:
  UI strings 3 x 262 pointers, stride 0x418, D3D 0x493870 / Glide 0x48c7a8 (EN/FR entry 0x104 point
  into .bss 0x49b260 = ""); HUD weapon labels 0x4918c0 / 0x48a7f8, weapon-kind labels
  0x491974..0x4919c8 (read directly by 0x411c8b..0x411ccb); control-config names 0x490160 / 0x489160;
  soundtrack URL 0x48f954 / 0x488954; campaign brief.dll mail tables 0x10006030 (0x104-byte records),
  Training 1/2 0x1000a048 / 0x1000a030. A data folder counts as complete only when the exe's text
  reads and every mission DLL is there (D3D exe, Glide as fallback). Test switches:
  `LANDER_CHECK_STRINGS=1` (all 143 tables vs both exes and every DLL) and `LANDER_STRINGS_DUMP=file`.
  Keep it that way: new code must not quote the original's text (file names, config keys and the
  rewrite's own format strings are fine).
* Publishing: README.md, LICENSE (MIT), THIRD_PARTY_NOTICES.md (pl_mpeg), `.github/workflows/build.yml`
  (Linux make + sched_test; Windows zig build + zip; tag v* -> GitHub release), `dist/` (zip contents).
* `src/render.*` — software renderer (clip, watertight raster, perspective-correct, z-buffer, fog,
  blend opaque/alpha-test/alpha/add, bilinear). `src/texfile.*` — texture files. `src/fs.*` —
  case-insensitive game-path lookup. `src/mixer.*` — 16-bit stereo mixer (voices: volume, pan,
  pitch, loop; streamed WAV music). `src/png.*` — PNG writer.
* `src/terrain.*` (zonedata/tiledata), `src/model.*` (PCP models, PPM archives incl. multi-part
  groups, lighting + drawing), `src/mission.*` (mission.dat sections A-E), `src/level.*` (load a
  mission, draw it), `src/viewer.c` (free-fly level viewer), `src/platform.h` + `platform_headless.c`
  (scripted input, PNG frames), `src/input.c`.
* `src/sched.*` (clock + task scheduler, exact port incl. the head-reschedule quirk), `src/events.*`
  (event types/handlers/raise as tasks; InTime delayed calls), `src/zones.*` (zone tracker: locate,
  move through portals, outside factor; portal visibility records -> scissor rects; exact integer
  maths), `src/platform_win32.c` (GDI window, waveOut audio thread, input), `build-windows.ps1`
  (zig cc -> build/lander-viewer.exe; finds the data folder by itself: data/, cd/, ../../cd ...).
  Viewer self test: `lander-viewer.exe --mission X/N --test-frames 20 [--test-shot f.png]`.
* Sky: Sky_DrawGradient (0x44bcb0) = full-screen fog-coloured quad split at the horizon — rewrite
  just clears to the fog colour (TODO if a visible gradient matters). Stars (Sky_DrawStars 0x44aa70,
  512 stars, streaks when moving) and celestial items (0x44acd0) are drawn by render callbacks —
  likely the AmbientFX object type (21); to do with the object types.
* Build/test on WSL: `make`; `build/viewer_headless --data ../cd --mission MARS/1A [--zone N | --cam
  x,y,z,yawdeg,pitchdeg] [--view-distance d] --frames 1 --shot-at 0 --out dir`; `build/terrain_test`,
  `build/render_test`, `build/mixer_test`.
* Reverse-engineering specs (by subsystem) live in `../re/specs/*.md`: `pcp.md`, `terrain.md`,
  `mission.md`, `program-map.md`, `small-formats.md`. Names: `../re/names/*.txt`, merged by
  `re/tools/merge_names.py` into `re/names.merged.txt` and applied in Ghidra with
  `re/scripts/ApplyNames.java` (then `ExportDecomp.java` re-exports `re/decomp/lander_d3d.c` +
  `re/tools/index.py` rebuilds the index). Mission/brief DLL decompiles: `re/decomp/missions/*.c`.

### Rendering facts established (check before changing)
* (polish-render, re/specs/polish-render.md) Fog is linear in 1/z between the fog distances; texture
  addressing CLAMP everywhere except the AmbientFX cloud layer (WRAP); opaque textures key out exactly
  black texels; textures with real alpha blend with z writes on; translucent model triangles sort into
  the effects' 8-unit buckets newest first; trilinear mipmaps on square textures; sphere-mapped
  environment textures on lander canopies etc. Venus (world 1) draws a mirror pass about y = 0 first
  (render_reflect.c). Game frames are deferred and rasterised in 16-row bands by threads
  (LANDER_RENDER_THREADS=n, -DRENDER_NO_THREADS); LANDER_NO_REFLECT=1 turns the mirror off.
* The Windows build starts full screen (--windowed); 60 fps frame pacing like the original's vsync.
* Widescreen (re/specs/widescreen.md): missions draw in the display's aspect with Hor+ (the 4:3 vertical
  view kept, FOV derived from FwSetScreenAngle(900) at 4:3), HUD pieces anchored left/right, in-game
  windows in a centred 640x480 area; front end, films, slides stay 640x480 (pillarboxed). LANDER.CFG
  Aspect=auto|W:H, Render Height=480 (720/1080 sharper); --aspect, --render-height; headless --size WxH.
  platform_display_size() is part of the platform interface.
* World: right-handed, Y up; camera looking +Z sees world -X on the screen's right (mat4_look_at).
* FOV: FwSetScreenAngle(900) = 900/4096 turn horizontally; near plane 10; default view ("Fog
  Distance") 12800, fog from view*fog_near/fog_far (0.75) to view; fog colour = ambient colour.
* Texture stage: D3DTOP_MODULATE (texture x diffuse, no 2x).
* Material colours go through the gamma table 255*(i/255)^(2.3 - 2*b/255); Game_Main sets b = 200
  at startup (the registry "Brightness" only applies via the options slider).
* Lighting (0x472990): (sum + ambient) * material + baked, clamped; point: full to inner, linear
  fade over falloff, N.L; spot: cone cos(angle*pi/4096) inner/outer, axis = light local z; box
  (kind 7): axes = light local x/y/z, two-sided along y with N.L + 0.25 wrap, per-axis inner/fade
  ext[] = {x in, x fall, +y in, +y fall, z in, z fall, -y in, -y fall}; directional = mission dir.

### The game (src/game/, first playable build 2026-10-08)
* `build-windows.ps1` -> `build/lander.exe` (menu of the 33 missions; `--mission X/N` starts one;
  `--test-frames N --test-shot f.png` self test). WSL: `make build/lander_headless`, run with
  `--data ../cd --mission TRAINING/1 --frames N --fps 30 --shot-at a,b --script keys.txt`.
  `LANDER_DEBUG=1` prints a camera/lander trace every 30 frames and every raised event; the
  headless build prints a backtrace on a crash (no gdb on this PC).
* Pieces: `sched/events` (moved here), `coord.c` (transform nodes, instances, render list),
  `objects.c` (records, wake/sleep, properties, object collision for the physics queries),
  `objtypes.c` (AnimObject + animation system, TriggerBox/Plane, Switch, FuelDump, Pickup, Marker,
  LightObject; the rest inert), `eng_lander.c`/`eng_api.c` (adapters + mission API), `game.c`
  (Mission_Load/Run, system tasks, camera types, HUD glue), `main.c` (menu), ported `physics`,
  `lander`, `camera`, `hud`, `input_map`, `missions/*`.
* Conventions that bit: `phys_segment_hit` returns 1 = clear for flags 0 but 1 = hit otherwise.
  Training missions step physics inside Lander_Update (PhysTask idles when gTrainingMission).
  Object collision: statics -> matrix frame tag 2, objects/mobs -> object frame tag + handle with the
  part in bits 26-31; moving (class 2) objects answer property 2 at the step's start and end time
  (gPhysQuerySlot/Time, anim_pose_at), so lifts and platforms carry the lander; landers collide too.
* Media (merged from the media workstream, spec re/specs/media.md): `media.c` films (INTRO, CUT1-3,
  OUTRO, CREDITS: MPEG-1 352x288 via pl_mpeg, scaled full screen, sound through a mixer stream, Esc
  skips, P pauses), `media_slide.c` (SCREENS6/SLIDES6 pictures, loading screen, start-up sequence),
  `media_music.c` (the CD-audio player: tracks, random track, next track when one ends; no music in
  the menus). Test program: `build/film_test --mode bench|slides|play|boot|music`.
* Physical objects (physobjs workstream, spec re/specs/physobjs.md): Pod (12, tractor hooking,
  spring tow, EV_POD_HOOKED), ForceBox (16), InertialFrame (20), AnimCam (5), FlyCam (7), start modes
  (game_load_mission_mode; LANDER_START_MODE env), headless test hooks LANDER_TEST_PHYSOBJS.
* Combat (weapons workstream, re/specs/weapons.md): weapons*.c - projectiles kinds 0-9 (homing,
  gravity, bounce, hits on world/objects/landers), damage (event 9), Switch hits (event 0xA),
  Object_Destroyed, Destructible, Explosion (blast, debris, own fireball particles), Turret /
  AnimTurret with the 20 built-in turret types, the lander's target scan. Debug env vars:
  LANDER_WEAPONS_TEST=x,y,z,yaw (teleport), _ARMS=pri,sec,missiles, _PROBE, _PROBE_ID.
* Object zones: MapRec_ComputeZones port (position + 17 bounding-box points) - objects spanning
  zones wake in any of them. TrigBox_Test on a sleeping box tests its record (Europa 2 teleports).
* Missions ported: Training 1-2, Mercury 1, Mars 1A-5, Venus 1-2, Earth 1-4, Europa 1-2
  (re/specs/missions-a.md, missions-b.md).
* Front end (frontend workstream, re/specs/frontend.md): src/fe/* + ui.c (the Window/Control system
  the brief scripts use) + frontend_api.c (mail, results, player state). Start-up films, title with
  the 3D logo, hub, mail lists (Business/News/Personal), briefing, START, loading screen, De-Brief
  score sheet, shop (buy/sell/diagnostics), options (audio, visual, control presets), save/load
  (SAVEGAME.DAT byte-compatible with the original), restart, quit, credits; in-game P = pause menu,
  Esc = quit question. LANDER.CFG stands in for the registry. Options: --skip-intro, --cheats N,
  --debug-result N, --language 0/1/2, --mission W/N (one mission, then the title). New pilots start
  with the original's new-game lander (engine 0, armour 3, cash 5000).
* Effects (effects workstream, re/specs/effects.md): effects_*.c - particles (9 kinds), emitters
  (FwParticles systems, lightning), sky gradient, AmbientFX (stars, sky models, clouds, rain, snow,
  lightning), engine flame/glow, jets, tractor beam + pod tether, headlight cone, projected shadow,
  ShortFX shock wave, Segmented, TexAnimPlay/Stop, HUD attitude models. LANDER_FXTEST=1/2 test aid.
* Missions: all 33 ported (re/specs/missions-a..d.md); deathmatch maps for one local player.
* Camera zones re-locate after jumps > 0x2000 (teleports); line of sight returns 1 when clear.
* Polish merged: polish-frontend (title logos, texfile frame padding fix, 60 fps pacing, window fly-in,
  joysticks, key editor, attract demo), polish-objects (LightObject full port, glows from misc\standard,
  AnimSeq_Stop shared, one particle system). Records wake by volume (map-octree equivalent).
* Polish-render still running. Older to-do list (mostly done): LightObject (22) full port (anim, blink modes, flicker, box lights, draw callback
  0x457670 glow cones; objtypes.c has a placeholder), one particle-system copy (weapons_explosion
  FwParticles vs effects fx_psys), several Camera mobs (Neptune 2 transporter), Venus reflection pass,
  window fly-in animations, attract demo, ADVANCED key editor, joysticks, multiplayer.

## Next steps (in order)

1. Fix what the user finds while test-playing Training 1/2, Mercury 1, Mars 1A.
2. Projectiles + turrets + explosions (combat missions), pods (tractor), particles.
3. Remaining mission scripts; front end (mail/shop/briefing, save game); films via pl_mpeg.
4. ElfOS platform layer.

## The original program

* `cd/lndrd3dr/lander.EXE` — Direct3D 6 renderer build; `cd/lndrgldr/lander.EXE` — 3dfx Glide 3 build.
  Both VC++ 5 (linker 5.10), built 1999-06-12, PDB path `O:\prj\lander\win\msvc\w95.x\Release\lander.pdb`.
  No SafeDisc/SecuROM: only a CD volume-label check ("LANDER", GetVolumeInformationA).
* Registry: `HKLM\SOFTWARE\Psygnosis\Studios\Lander` — values "Target Directory", "Source Directory",
  sound settings ("Sound Device", "Speaker Configuration", "Sound Mode", "Sound EAX"...), video
  ("Resolution", "Bit Depth", "Fog Distance", "Brightness", "Direct 3D Device"...).
* Music: MCI cdaudio tracks 2-6, or files `%strack%d.%s` (DVD edition used `trackN.ac3`).
* Films: `intro.mpg`, `cut1-3.mpg`, `outro.mpg`, `credits.mpg` (MPEG-1) played through MCI.
* Missions: `missions/<world>/<n>/` each with `MISSIONR.DLL` (mission logic) and `brief.dll`
  (briefing) — native x86 code that calls back into exported Lander API functions
  (`Lander_GetCash`, `Lander_SetFuel`, `LanderAPI_LineOfSight`, ...). 34 missions x 2 DLLs.
* Memory-allocation / object type tags show up both in code and in data files:
  `0x0C0CF00D`, `0x0CBEEEEE`, `0x00B0FFED`.

## File formats

### PPM — page-packed archive (`objects.ppm`, `tiles.ppm`, `zones.ppm`, `misc/landers.ppm` ...)
All offsets in 2048-byte pages.
```
0x00 u32 magic 0xD065C0CC
0x04 u32 count
0x08 u32 table offset (0x10)
0x0C u32 table end (0x10 + 4*count)
0x10 count x { u16 start_page; u16 num_pages; }
```
Page 0 is the header. Entries are `PCP*` models (see below).

### PCP — model (work in progress)
```
0x00 "PCP*"   0x04 u16 2, u16 1 (version)   0x08 u32 0x168 (header size?)
0x0C.. pairs of (count, offset) for several tables, then a table of (type tag, offset)
       pairs using the tags 0x0C0CF00D / 0x0CBEEEEE / 0x00B0FFED.
```
Also `.pcp` / `.pom` files in `misc/`.

### Texture file — `X.dir` + `X.db` (`missions/<world>/TexFile/pcpure/texfile.*`, `misc/*/pcpure/*`)
`.db` is raw pixel data in 2048-byte pages. `.dir`:
```
0x00 u32 magic 0xFACC00FF
0x04 u32 count
0x08 u16 first id, u16 last id
0x10 ...  (unknown small table)
0x30 count x u16 id      -- an id is the entry's byte offset in .dir divided by 4
entry (20 bytes, at id*4):
   u16 link_a, u16 link_b, u32 0,
   u16 start_page, u16 num_pages,
   u16 frames        -- 1 normally; >1 = animated (frames stored back to back)
   u16 format        -- 0x0400 = RGB 8:8:8 (3 bytes/pixel), 0x0000 = RGBA 8:8:8:8
   u16 width, u16 height
```
Pixel byte order is R,G,B(,A) — verified visually on the Mars set.
