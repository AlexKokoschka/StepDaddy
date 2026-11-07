#!/bin/bash
# Puts the ARM compiler (arm-none-eabi-gcc, with newlib) on PATH for the
# firmware build, plus cmake and ninja.
#
# It used to come from Ubuntu (gcc-arm-none-eabi through apt). That download,
# a few hundred MB, stalled on Ubuntu's mirror for 5+ minutes at a time on
# some runners in Oct 2026 (the apt notices showed the package list arriving
# in seconds and the install timing out). Now it is ARM's own release,
# downloaded once and kept in GitHub's cache (actions/cache, see the
# workflow), so most runs don't download it at all.
#   .github/scripts/arm-toolchain.sh <install dir>
set -eu
DIR=${1:?install dir}
REL=13.2.rel1
URL="https://developer.arm.com/-/media/Files/downloads/gnu/$REL/binrel/arm-gnu-toolchain-$REL-x86_64-arm-none-eabi.tar.xz"

if [ ! -x "$DIR/bin/arm-none-eabi-gcc" ]; then
  echo "::notice title=toolchain::downloading ARM GNU toolchain $REL"
  mkdir -p "$DIR"
  t0=$(date +%s)
  for attempt in 1 2 3; do
    if curl -fsSL --retry 3 --connect-timeout 30 --max-time 600 "$URL" | tar -xJ -C "$DIR" --strip-components=1; then
      break
    fi
    echo "download attempt $attempt failed"
    rm -rf "${DIR:?}"/*
    [ "$attempt" = 3 ] && { echo "::error title=toolchain::could not download $URL"; exit 1; }
  done
  echo "::notice title=toolchain::downloaded in $(($(date +%s) - t0)) s"
fi
echo "$DIR/bin" >> "$GITHUB_PATH"
"$DIR/bin/arm-none-eabi-gcc" --version | head -1

# cmake and ninja come with the runner image; only if one is missing:
command -v cmake >/dev/null && command -v ninja >/dev/null || pip install --user cmake ninja
