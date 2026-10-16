#!/usr/bin/env bash
# Jet Force Gemini: Recompiled: build the game from your own ROM and install it.
#
#   ./install.sh path/to/your/rom.z64    build and install (run it again after `git pull` to update)
#   ./install.sh --uninstall             remove the installed game (keeps your saves and settings)
#
# Your ROM must be Jet Force Gemini (USA), any byte order (.z64, .v64, .n64); it is checked before anything is built.
# The game goes to ~/.local/share/jfg-recomp, with a `jet-force-gemini` command in ~/.local/bin and an entry in your
# applications menu. Saves and settings live in ~/.config/JFGRecompiled.
set -euo pipefail

dest="${JFG_PREFIX:-$HOME/.local/share/jfg-recomp}"
bin_dir="$HOME/.local/bin"
launcher="$bin_dir/jet-force-gemini"
desktop="$HOME/.local/share/applications/jfg-recomp.desktop"
config="$HOME/.config/JFGRecompiled"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

if [[ $# -ne 1 ]]; then usage 1; fi
case "$1" in
    -h|--help) usage 0 ;;
    --uninstall)
        rm -rf "${dest:?}" "$launcher" "$desktop"
        say "Removed the game. Your saves and settings are still in $config"
        exit 0 ;;
esac
[[ -f "$1" ]] || die "no such file: $1"
rom_in="$(realpath "$1")"
cd "$(dirname "$0")"

[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || die "this port runs on 64-bit x86 Linux only"
[[ -d .git ]] || die "run this from a git clone (git clone --recurse-submodules ...), not a downloaded archive"

# --- Build dependencies -------------------------------------------------------------------------------------------
# tools/toolchain.env picks up a project-local toolchain in .toolchain/ if there is one; otherwise the system's.
source tools/toolchain.env

missing=()
for cmd in git cmake ninja make clang clang++ ld.lld pkg-config python3; do
    command -v "$cmd" > /dev/null || missing+=("$cmd")
done
if command -v pkg-config > /dev/null; then
    for lib in sdl2 gtk+-3.0 freetype2; do
        pkg-config --exists "$lib" 2> /dev/null || [[ "$lib" == sdl2 && -n "${SDL2_DIR:-}" ]] || missing+=("$lib")
    done
fi
if command -v python3 > /dev/null; then
    python3 -c 'import sys; sys.exit(sys.version_info < (3, 12))' || missing+=("python3.12+")
    python3 -c 'import venv, ensurepip' 2> /dev/null || missing+=("python3-venv")
fi

if [[ ${#missing[@]} -gt 0 ]]; then
    say "Missing build tools: ${missing[*]}"
    if command -v apt-get > /dev/null; then
        install=(sudo apt-get install -y git cmake ninja-build make g++ clang lld llvm pkg-config python3-venv
                 libsdl2-dev libgtk-3-dev libfreetype-dev)
    elif command -v dnf > /dev/null; then
        install=(sudo dnf install -y git cmake ninja-build make gcc-c++ clang lld llvm pkgconf-pkg-config python3
                 SDL2-devel gtk3-devel freetype-devel)
    elif command -v pacman > /dev/null; then
        install=(sudo pacman -S --needed git cmake ninja make gcc clang lld llvm pkgconf python sdl2 gtk3 freetype2)
    else
        die "install them with your package manager (clang, lld, cmake, ninja, SDL2, GTK 3 and FreeType development files, Python 3.12+), then run this again"
    fi
    echo "They can be installed with:"
    echo "    ${install[*]}"
    answer=n
    if [[ -t 0 ]]; then read -r -p "Run that now? [Y/n] " answer || true; answer="${answer:-y}"; fi
    [[ "$answer" == [Yy]* ]] || die "install them, then run this again"
    "${install[@]}"
fi

# --- Sources --------------------------------------------------------------------------------------------------------
say "Fetching libraries (git submodules)"
git submodule update --init --recursive
scripts/apply-lib-patches.sh > /dev/null

say "Setting up romtool (Python)"
if [[ ! -x tools/romtool/.venv/bin/romtool ]]; then
    python3 -m venv tools/romtool/.venv
    tools/romtool/.venv/bin/pip install --quiet -e tools/romtool
fi

say "Checking your ROM"
tools/romtool/.venv/bin/romtool verify "$rom_in"

# --- Build ----------------------------------------------------------------------------------------------------------
compilers=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
say "Building the recompiler"
if [[ ! -x build/tools/N64Recomp/N64Recomp || ! -x build/tools/N64Recomp/RSPRecomp ]]; then
    cmake -S lib/N64Recomp -B build/tools/N64Recomp -G Ninja -DCMAKE_BUILD_TYPE=Release "${compilers[@]}" > /dev/null
    cmake --build build/tools/N64Recomp
fi
make -s -C lib/n64sym > /dev/null

say "Recompiling the game from your ROM"
JFG_ROM="$rom_in" scripts/recompile.sh

say "Compiling (this takes a while the first time)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "${compilers[@]}" > /dev/null
cmake --build build --target JFGRecompiled

# --- Install --------------------------------------------------------------------------------------------------------
say "Installing to $dest"
mkdir -p "$dest" "$bin_dir" "$(dirname "$desktop")" "$config"
rm -rf "${dest:?}/assets"
cp build/JFGRecompiled "$dest/"
cp -r assets "$dest/"
cp recompcontrollerdb.txt "$dest/"
cp icons/512.png "$dest/icon.png"

# The game remembers the ROM picked in its launcher as this file; putting it there skips that step.
cp generated/jfg.z64 "$config/jfg.n64.us.z64"

cat > "$launcher" <<EOF
#!/bin/sh
# Jet Force Gemini: Recompiled (installed by install.sh). The game finds its assets in the working directory.
cd "$dest" && exec ./JFGRecompiled "\$@"
EOF
chmod +x "$launcher"

cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Jet Force Gemini: Recompiled
Comment=Native PC port of Jet Force Gemini with modern controls
Exec=$launcher
Icon=$dest/icon.png
Terminal=false
Categories=Game;
EOF

say "Done. Start it from your applications menu, or run: jet-force-gemini"
case ":$PATH:" in
    *":$bin_dir:"*) ;;
    *) echo "($bin_dir is not on your PATH; run $launcher, or add it to PATH)" ;;
esac
