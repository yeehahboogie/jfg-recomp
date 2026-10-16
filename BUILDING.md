# Building by hand

`./install.sh` (see the [README](README.md)) does all of this for you. These are the same steps, for working on the
port.

## What you need

- 64-bit x86 Linux. The commands below are for Ubuntu 24.04; other distributions have the same packages under
  similar names.
- Your ROM: *Jet Force Gemini* (USA), in any byte order. Put it at `jfg.z64` in the repository root (it's
  git-ignored), or point `JFG_ROM` at it.

```bash
sudo apt-get install -y git cmake ninja-build make g++ clang lld llvm pkg-config python3-venv \
    libsdl2-dev libgtk-3-dev libfreetype-dev
```

Without root, unpack the same packages into `.toolchain/root` (`apt-get download` and `dpkg -x`) and
`source tools/toolchain.env` in each shell. The scripts pick it up.

## Steps

```bash
git clone --recurse-submodules https://github.com/tallsam/jfg-recomp.git && cd jfg-recomp
scripts/apply-lib-patches.sh                      # our patches to RT64 and N64ModernRuntime (lib-patches/)
python3 -m venv tools/romtool/.venv && tools/romtool/.venv/bin/pip install -e 'tools/romtool[test,disasm]'
cmake -S lib/N64Recomp -B build/tools/N64Recomp -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/tools/N64Recomp
make -C lib/n64sym
scripts/recompile.sh                              # ROM -> ELF -> C and RSP code in generated/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target JFGRecompiled
```

Run it from the repository root (it loads `assets/` from the working directory):

```bash
APP_FOLDER_PATH=$PWD/build/appdata build/JFGRecompiled
```

`APP_FOLDER_PATH` keeps settings and saves in `build/appdata` instead of `~/.config/JFGRecompiled`. The first run asks
for the ROM in the launcher.

After changing `recomp/` (hooks, function overrides) or `tools/romtool`, run `scripts/recompile.sh` again before
building. `generated/` and `build/` are derived from your ROM: never commit them.

## Tests

```bash
scripts/test-hooks.sh                              # native unit tests (no ROM needed)
tools/romtool/.venv/bin/pytest -q tools/romtool    # romtool tests (ROM tests are skipped without jfg.z64)
scripts/smoke.sh                                   # boots the game for 90 s and checks the log
```

## Layout

| Path | What's there |
|---|---|
| `tools/romtool/` | Turns the ROM into a MIPS ELF for N64Recomp: main code, the 157 relocatable modules and their relocations. |
| `recomp/` | N64Recomp configuration: hooks into game functions, function overrides, the audio microcode. |
| `src/jfg/` | The port's game-side code: fixes, controls (`controls/`), cheats, big heads, level tools. |
| `src/game/`, `src/ui/`, `src/main/` | The frontend: input, configuration, menus, launcher. |
| `lib-patches/` | Patches applied to the RT64 and N64ModernRuntime submodules. |
| `scripts/` | Build, recompile, test and survey scripts. |
| `tools/diag/` | Debugging tools: display-list walker, rasteriser, memory and audio inspection. |
| `docs/superpowers/` | Development notes: design specs, plans, findings about the game, and the debugging switches. |

The debugging switches (`JFG_DEBUG_*`, `JFG_INPUT_SCRIPT`, `JFG_WARP` and others) are listed in
[docs/superpowers/handoff-2026-10-14.md](docs/superpowers/handoff-2026-10-14.md).
