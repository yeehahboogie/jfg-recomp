# Jet Force Gemini: Recompiled

A native Linux port of Rare's *Jet Force Gemini* (Nintendo 64, 1999), made by statically recompiling the game into
a PC program. Its main extra is modern controls: move with one stick, look and aim with the other (or the mouse),
and walk while you aim.

**This repository contains no game data.** To play, you need your own copy of the game: a dump of the
*Jet Force Gemini* (USA) cartridge. The installer builds the port from it on your machine.

## Features

- **Modern controls** (on by default): twin-stick on a gamepad, or mouse and keyboard. A free camera on the right
  stick or mouse, a hold-to-aim mode with a mouse-driven crosshair, and walking and strafing while aiming. The original
  N64 controls are one setting away.
- **Rebindable controls** for gamepad, keyboard and mouse.
- **Rendering with [RT64](https://github.com/rt64/rt64):** your screen's resolution and widescreen, with 2D screens
  pillarboxed.
- **Multiplayer:** extra gamepads become players 2–4, with split screen.
- **Cheats tab:** invulnerability, all weapons, all levels, infinite ammo, big heads, refill health, a level select,
  and Skip Ahead (leave the current level through any of its exits).
- **Stats overlay** (F3): frame rate, level and position.

## Status

The game boots, plays through the front end, intro and Goldwood, and has been checked level by level by automated
runs: every level loads, every exit leads where it should, and a two-minute combat run in each of the 318 playable
levels finished without a crash. Saves work, as does audio.

Not yet confirmed by a full playthrough: story events in their normal order, every boss fight, the ending, and Floyd
co-op. Reports are welcome (see [CONTRIBUTING.md](CONTRIBUTING.md)).

Known limitations:

- The game runs at its own 30 frames per second; there's no frame interpolation yet.
- Linux on 64-bit x86 only.
- With widescreen "Expand", the 2D vignette still covers only the 4:3 area.

## Install

You need 64-bit x86 Linux with a Vulkan-capable graphics card, about 3 GB of free disk space for the build, and your
ROM. The installer has been tested on Ubuntu 24.04; Fedora and Arch package names are included but untested.

```bash
git clone --recurse-submodules https://github.com/tallsam/jfg-recomp.git
cd jfg-recomp
./install.sh ~/path/to/your/jet-force-gemini.z64
```

The installer:

1. checks your ROM is *Jet Force Gemini* (USA): any byte order (`.z64`, `.v64`, `.n64`), MD5
   `772cc6eab2620d2d3cdc17bbc26c4f68` once in big-endian order;
2. lists any build tools you're missing and offers to install them with your package manager (it asks before using
   `sudo`);
3. recompiles the game from your ROM and compiles it, which takes a while the first time;
4. installs it to `~/.local/share/jfg-recomp`, with a `jet-force-gemini` command and an entry in your applications
   menu.

To update: `git pull`, then run `./install.sh` with your ROM again. To remove the game: `./install.sh --uninstall`
(your saves stay).

## Playing

Start it from your applications menu or with `jet-force-gemini`, then choose **Start game**.

- **Esc** opens the port's menu: graphics, controls, sound, general settings and the Cheats tab.
- **F3** toggles the stats overlay.
- Saves and settings are kept in `~/.config/JFGRecompiled`.

### Default controls (Modern)

| Action | Gamepad | Mouse and keyboard |
|---|---|---|
| Move | left stick | WASD |
| Look / aim | right stick | mouse |
| Fire | RT | left button |
| Aim | LT | right button |
| Jump | A | Space |
| Crouch / roll | B | Left Ctrl |
| Previous / next weapon (zoom when aiming a zoom weapon) | LB / RB | wheel, Q / E |
| Quick weapons 1–4 | D-pad | 1–4 |
| Strafe left / right | X / Y | ← / → |
| L (menus) | left stick click | Tab |
| Pause | Start | Enter |

The General tab holds the control scheme (Modern or Original), camera drift-back, look speed, aim sensitivity, axis
inversion and hold-or-toggle aiming.

## How it works

[N64Recomp](https://github.com/N64Recomp/N64Recomp) translates the game's MIPS code into C, which is compiled into
a native program running on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), with RT64 drawing
the graphics. *Jet Force Gemini* loads most of its code at run time as 157 relocatable modules, so
`tools/romtool` first rebuilds the ROM's code into an ELF that the recompiler can work from. Small hooks in
`src/jfg/` fix what a straight translation gets wrong and add the controls, cheats and other extras.

This project is built on [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp): we used its frontend
(launcher, menus, input and configuration) as our starting point and built the port on top of it.

[BUILDING.md](BUILDING.md) covers building by hand and the development tools.

## Credits and license

- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp): the recompiler and runtime.
- [RT64](https://github.com/rt64/rt64): the renderer.
- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp): the frontend this port is built on.
- [RmlUi](https://github.com/mikke89/RmlUi), [lunasvg](https://github.com/sammycage/lunasvg),
  [n64sym](https://github.com/shygoo/n64sym), and [PromptFont](https://shinmera.com/promptfont).
- Developed with [Claude Code](https://claude.com/claude-code).

Licensed under the GNU General Public License v3.0 (see [COPYING](COPYING)), as is Zelda64Recomp, on which it builds.

*Jet Force Gemini* is a trademark of its owners. This project is not affiliated with or endorsed by Rare or
Nintendo, and includes none of their game data.
