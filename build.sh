#!/usr/bin/env bash
# ./build.sh        build the app, the plugin and (with makerom + bannertool) the cia
# ./build.sh clean
set -euo pipefail
cd "$(dirname "$0")"

docker image inspect controlmole-3ds >/dev/null 2>&1 || docker build -t controlmole-3ds .
run() { docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w "/src/$1" controlmole-3ds make "${@:2}"; }
run . "$@"
run plugin "$@"
[ "${1:-}" = clean ] && { rm -f ControlMole.cia; exit 0; }

MAKEROM=${MAKEROM:-$HOME/Tools/makerom/makerom}
BANNERTOOL=${BANNERTOOL:-$HOME/Tools/bannertool/bannertool}
[ -x "$MAKEROM" ] && [ -x "$BANNERTOOL" ] || exit 0

mkdir -p build
"$BANNERTOOL" makesmdh -s ControlMole -l "ControlMole: Mole keeps your play time in check" \
  -p "Kairi Labs" -i res/icon48.png -o build/cia.smdh >/dev/null
"$BANNERTOOL" makebanner -i res/banner.png -a res/banner.wav -o build/banner.bnr >/dev/null
"$MAKEROM" -f cia -o ControlMole.cia -rsf app.rsf -target t -elf ControlMole.elf \
  -icon build/cia.smdh -banner build/banner.bnr \
  -DAPP_TITLE=ControlMole -DAPP_PRODUCT_CODE=CTR-H-CMOL -DAPP_ROMFS=romfs \
  -DAPP_CATEGORY=Application -DAPP_UNIQUE_ID=0xC0DE5 -DAPP_USE_ON_SD=true \
  -DAPP_ENCRYPTED=false -DAPP_MEMORY_TYPE=Application -DAPP_SYSTEM_MODE=64MB \
  -DAPP_SYSTEM_MODE_EXT=Legacy -DAPP_CPU_SPEED=268MHz -DAPP_ENABLE_L2_CACHE=false \
  2>&1 | grep -v "Failed to sign access descriptor" || true
