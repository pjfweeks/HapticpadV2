#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# HapticPad firmware toolchain setup (macOS, Apple Silicon or Intel)
#
# Installs everything needed to build the firmware WITHOUT requiring Homebrew
# or an administrator password.  Everything lands under $PICO_ROOT (default
# ~/pico) and $HOME/Library/Python, so uninstalling is a matter of deleting
# those directories.
#
#   ./tools/setup_macos.sh
#
# Afterwards, source the generated environment file:
#   source ~/pico/pico-env.sh
# ---------------------------------------------------------------------------
set -euo pipefail

PICO_ROOT="${PICO_ROOT:-$HOME/pico}"
SDK_VERSION="${SDK_VERSION:-2.1.1}"
ARM_VERSION="14.2.rel1"

case "$(uname -m)" in
    arm64)  ARM_HOST="darwin-arm64" ;;
    x86_64) ARM_HOST="darwin-x86_64" ;;
    *)      echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

ARM_DIR="arm-gnu-toolchain-${ARM_VERSION}-${ARM_HOST}-arm-none-eabi"
ARM_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_VERSION}/binrel/${ARM_DIR}.tar.xz"

say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }

mkdir -p "$PICO_ROOT"

# --- 1. Command Line Tools (git, clang for host tools) ---------------------
if ! xcode-select -p >/dev/null 2>&1; then
    say "Xcode Command Line Tools missing"
    echo "Run 'xcode-select --install', complete the installer, then re-run this script."
    exit 1
fi

# --- 2. CMake + Ninja via pip (no Homebrew, no sudo) -----------------------
say "Installing CMake and Ninja (pip --user)"
python3 -m pip install --user --upgrade --quiet pip
python3 -m pip install --user --quiet cmake ninja
PY_BIN="$(python3 -c 'import site,os;print(os.path.join(site.USER_BASE,"bin"))')"
export PATH="$PY_BIN:$PATH"
cmake --version | head -1

# --- 3. ARM bare-metal toolchain -------------------------------------------
if [ ! -x "$PICO_ROOT/$ARM_DIR/bin/arm-none-eabi-gcc" ]; then
    say "Downloading ARM GNU toolchain ${ARM_VERSION} (${ARM_HOST}, ~500 MB unpacked)"
    curl -L --fail --progress-bar -o "$PICO_ROOT/arm.tar.xz" "$ARM_URL"
    say "Extracting"
    tar -xf "$PICO_ROOT/arm.tar.xz" -C "$PICO_ROOT"
    rm -f "$PICO_ROOT/arm.tar.xz"
    # macOS quarantines downloaded binaries; strip the attribute so they run.
    xattr -dr com.apple.quarantine "$PICO_ROOT/$ARM_DIR" 2>/dev/null || true
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

# --- 5. Environment file ----------------------------------------------------
cat > "$PICO_ROOT/pico-env.sh" <<EOF
# Source this file before building the HapticPad firmware.
export PICO_SDK_PATH="$PICO_ROOT/pico-sdk"
export PICO_TOOLCHAIN_PATH="$PICO_ROOT/$ARM_DIR"
export PATH="$PICO_ROOT/$ARM_DIR/bin:$PY_BIN:\$PATH"
EOF

say "Done"
cat <<EOF

Add this to your ~/.zshrc (or run it in each new shell):

    source $PICO_ROOT/pico-env.sh

Then build the firmware:

    cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    cmake -S . -B build -G Ninja
    cmake --build build

Flash: hold BOOTSEL, plug in the pad, drag build/hapticpad.uf2 onto RPI-RP2.
EOF
