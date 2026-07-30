#!/usr/bin/env bash
# Packages the built Switch ELF into an NRO: nacptool (version from
# res/version.txt) + strip + elf2nro (with the app icon). Safe to re-run.
set -euo pipefail
: "${DEVKITPRO:=/opt/devkitpro}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

switch_build_dir="${SWITCH_BUILD_DIR:-build/switch-app}"
out="$switch_build_dir/UnleashedRecomp"
elf="$out/UnleashedRecomp"
[ -f "$elf" ] || { echo "ELF not found: $elf (build the app first)." >&2; exit 2; }

# shellcheck disable=SC1091
. UnleashedRecomp/res/version.txt
version="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}"

icon="UnleashedRecomp/res/switch_icon.jpg"
[ -f "$icon" ] || icon="$DEVKITPRO/libnx/default_icon.jpg"

"$DEVKITPRO/tools/bin/nacptool" --create "Unleashed Recompiled" "hedge-dev" "$version" "$out/UnleashedRecomp.nacp"

cp "$elf" "$out/UnleashedRecomp.debug.elf"
cp "$elf" "$out/UnleashedRecomp.stripped"
"$DEVKITPRO/devkitA64/bin/aarch64-none-elf-strip" --strip-all "$out/UnleashedRecomp.stripped"
"$DEVKITPRO/tools/bin/elf2nro" "$out/UnleashedRecomp.stripped" "$out/UnleashedRecomp.nro" \
  --icon="$icon" --nacp="$out/UnleashedRecomp.nacp"

mkdir -p dist/switch
cp "$out/UnleashedRecomp.nro" dist/switch/UnleashedRecomp.nro
echo "NRO: dist/switch/UnleashedRecomp.nro  (v$version, icon: $(basename "$icon"))"
