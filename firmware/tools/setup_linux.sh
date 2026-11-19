#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# HapticPad firmware toolchain setup (Linux, x86_64 or aarch64)
#
# Installs everything needed to build the firmware WITHOUT root.  Everything
# lands under $PICO_ROOT (default ~/pico), so uninstalling is a matter of
# deleting that directory.
#
# Prerequisites from your distro's package manager: git, curl, tar, xz,
# python3, and a host C/C++ compiler (gcc/g++ or clang), e.g.
#   Arch:          sudo pacman -S --needed base-devel git curl xz python
#   Debian/Ubuntu: sudo apt install build-essential git curl xz-utils python3 python3-venv
#   Fedora:        sudo dnf install gcc gcc-c++ make git curl xz python3
# CMake and Ninja are used from the system if present, otherwise installed
# into a private Python venv under $PICO_ROOT.
#
#   ./tools/setup_linux.sh
#
# Afterwards, source the generated environment file:
#   source ~/pico/pico-env.sh
# ---------------------------------------------------------------------------
set -euo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/pico}"
SDK_VERSION="${SDK_VERSION:-2.1.1}"
ARM_VERSION="14.2.rel1"
CMAKE_MIN="3.13"

case "$(uname -m)" in
    x86_64)         ARM_HOST="x86_64" ;;
    aarch64|arm64)  ARM_HOST="aarch64" ;;
    *)              echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

ARM_DIR="arm-gnu-toolchain-${ARM_VERSION}-${ARM_HOST}-arm-none-eabi"
ARM_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_VERSION}/binrel/${ARM_DIR}.tar.xz"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

# --- 1. Host prerequisites (git, compiler for host tools like pioasm) ------
missing=()
for tool in git curl tar xz python3; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ! { command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1; }; then
    missing+=("C compiler (gcc or clang)")
fi
if ! { command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1; }; then
    missing+=("C++ compiler (g++ or clang++)")
fi
if [ "${#missing[@]}" -gt 0 ]; then
    say "Missing prerequisites"
    printf '  - %s\n' "${missing[@]}"
    echo "Install them with your package manager (see the header of this script), then re-run."
    exit 1
fi

mkdir -p "$PICO_ROOT"

# --- 2. CMake + Ninja (system if usable, else private venv) ----------------
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

PY_BIN=""
sys_cmake_ok=false
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    sys_cmake_ver="$(cmake --version | head -1 | awk '{print $3}')"
    if version_ge "$sys_cmake_ver" "$CMAKE_MIN"; then
        sys_cmake_ok=true
    fi
fi

if $sys_cmake_ok; then
    say "Using system CMake and Ninja"
else
    say "Installing CMake and Ninja into $PICO_ROOT/venv"
    # A venv sidesteps PEP 668 ("externally-managed-environment") on modern distros.
    if [ ! -x "$PICO_ROOT/venv/bin/python" ]; then
        if ! python3 -m venv "$PICO_ROOT/venv"; then
            echo "python3 -m venv failed (on Debian/Ubuntu install python3-venv)." >&2
            exit 1
        fi
    fi
    "$PICO_ROOT/venv/bin/python" -m pip install --upgrade --quiet pip
    "$PICO_ROOT/venv/bin/python" -m pip install --quiet cmake ninja
    PY_BIN="$PICO_ROOT/venv/bin"
    export PATH="$PY_BIN:$PATH"
fi
cmake --version | head -1
echo "ninja $(ninja --version)"

# --- 3. ARM bare-metal toolchain -------------------------------------------
if [ ! -x "$PICO_ROOT/$ARM_DIR/bin/arm-none-eabi-gcc" ]; then
    say "Downloading ARM GNU toolchain ${ARM_VERSION} (${ARM_HOST}, ~500 MB unpacked)"
    curl -L --fail --progress-bar -o "$PICO_ROOT/arm.tar.xz" "$ARM_URL"
    say "Extracting"
    tar -xf "$PICO_ROOT/arm.tar.xz" -C "$PICO_ROOT"
    rm -f "$PICO_ROOT/arm.tar.xz"
else
    say "ARM toolchain already present"
fi
"$PICO_ROOT/$ARM_DIR/bin/arm-none-eabi-gcc" --version | head -1

# --- 4. Pico SDK (with TinyUSB submodule) ----------------------------------
if [ ! -d "$PICO_ROOT/pico-sdk/.git" ]; then
    say "Cloning Pico SDK ${SDK_VERSION}"
    git clone --branch "$SDK_VERSION" --depth 1 \
        https://github.com/raspberrypi/pico-sdk.git "$PICO_ROOT/pico-sdk"
fi
say "Fetching SDK submodules (TinyUSB, ~200 MB)"
git -C "$PICO_ROOT/pico-sdk" submodule update --init --depth 1 lib/tinyusb

# SDK 2.1.1's pioasm (a host tool) uses uint8_t without <cstdint>, which
# GCC 15+ rejects.  Add the include; harmless on older compilers and a no-op
# once the SDK ships the fix (2.2.0+).
for hdr in pio_types.h output_format.h; do
    f="$PICO_ROOT/pico-sdk/tools/pioasm/$hdr"
    if [ -f "$f" ] && ! grep -q '#include <cstdint>' "$f"; then
        say "Patching pioasm/$hdr for newer GCC (missing <cstdint>)"
        sed -i '0,/^#include /s//#include <cstdint>\n#include /' "$f"
    fi
done

# --- 5. Environment file ----------------------------------------------------
EXTRA_PATH="$PICO_ROOT/$ARM_DIR/bin"
[ -n "$PY_BIN" ] && EXTRA_PATH="$EXTRA_PATH:$PY_BIN"

cat > "$PICO_ROOT/pico-env.sh" <<EOF
# Source this file before building the HapticPad firmware.
export PICO_SDK_PATH="$PICO_ROOT/pico-sdk"
export PICO_TOOLCHAIN_PATH="$PICO_ROOT/$ARM_DIR"
export PATH="$EXTRA_PATH:\$PATH"
EOF

case "$(basename "${SHELL:-bash}")" in
    zsh)  RC_FILE="$HOME/.zshrc" ;;
    fish) RC_FILE="your fish config (use 'bass' or translate the exports)" ;;
    *)    RC_FILE="$HOME/.bashrc" ;;
esac

say "Done"
cat <<EOF

Add this to $RC_FILE (or run it in each new shell):

    source $PICO_ROOT/pico-env.sh

Then build the firmware:

    cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    cmake -S . -B build -G Ninja
    cmake --build build

Flash: hold BOOTSEL, plug in the pad, and copy build/hapticpad.uf2 onto the
RPI-RP2 drive (most desktops auto-mount it under /run/media/\$USER/RPI-RP2
or /media/\$USER/RPI-RP2).
EOF
