#!/bin/bash
# build-003-translator.sh <folder>: builds the 0.0.3 shader translator into <folder> (XenosRecomp at the submodule's
# commit with only the MinGW DXC patch, which is all 0.0.3's build-switch.sh applied, plus add_dump_hooks.py:
# XENOS_RECOMP_DUMP_DIR and XENOS_RECOMP_ONLY as in the current translator). Then dump every shader with both:
#   XENOS_RECOMP_DUMP_DIR=<old> <folder>/build/XenosRecomp/XenosRecomp.exe UnleashedRecompLib/private <tmp.cpp> \
#     <folder>/XenosRecomp/shader_common.h
#   XENOS_RECOMP_DUMP_DIR=<new> build/host-tools-x64/tools/XenosRecomp/XenosRecomp/XenosRecomp.exe \
#     UnleashedRecompLib/private <tmp.cpp> tools/XenosRecomp/XenosRecomp/shader_common.h
# (with clang64 and the x64 DXC on PATH), and run audit.py <old> <new> <out.txt>. Run in the devkitPro MSYS2 shell.
set -e
X="$(mkdir -p "$1" && cd "$1" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$HERE/../.." && pwd)"
XR=$R/tools/XenosRecomp
XRW="$(cygpath -m "$XR")"
CLANG64="${CLANG64:-/c/devkitPro/msys2/clang64/bin}"
CMAKE="$CLANG64/cmake.exe"
NINJA="$CLANG64/ninja.exe"
DXC_X64="$XR/thirdparty/dxc-bin/bin/x64"

rm -rf "$X/XenosRecomp" "$X/build"
mkdir -p "$X/XenosRecomp"
( cd "$XR" && for f in $(git ls-tree --name-only HEAD XenosRecomp/); do git show "HEAD:$f" > "$X/$f"; done )
( cd "$X" && git apply "$R/patches/XenosRecomp-mingw-dxc.patch" && echo "applied the MinGW DXC patch" )
( cd "$X" && "$CLANG64/python.exe" "$HERE/add_dump_hooks.py" )

sed -i "s#\${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/smol-v/source#$XRW/thirdparty/smol-v/source#" "$X/XenosRecomp/CMakeLists.txt"
cat > "$X/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 17)
project("XenosRecomp-003")
set(XENOS_RECOMP_THIRDPARTY_ROOT "$XRW/thirdparty")
add_subdirectory(\${XENOS_RECOMP_THIRDPARTY_ROOT} thirdparty)
add_subdirectory("\${CMAKE_CURRENT_SOURCE_DIR}/XenosRecomp")
target_compile_definitions(XenosRecomp PRIVATE UNLEASHED_RECOMP)
EOF

PATH="$CLANG64:$DXC_X64:$PATH" CC="$CLANG64/clang.exe" CXX="$CLANG64/clang++.exe" \
"$CMAKE" -S "$X" -B "$X/build" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=AMD64 \
  -DCMAKE_BUILD_TYPE=Release -DXENOS_RECOMP_DXIL=OFF \
  -DCMAKE_CXX_FLAGS="-include cstdlib -include cstdio" -DCMAKE_MAKE_PROGRAM="$NINJA" > "$X/configure.log" 2>&1 \
  || { tail -30 "$X/configure.log"; exit 1; }
PATH="$CLANG64:$DXC_X64:$PATH" "$CMAKE" --build "$X/build" -j8 --target XenosRecomp > "$X/build.log" 2>&1 \
  || { tail -40 "$X/build.log"; exit 1; }
ls -la "$X/build/XenosRecomp/XenosRecomp.exe"
