add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/bc_diff)
add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/file_to_c)
add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/fshasher)
add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/x_decompress)
add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/XenonRecomp)
add_subdirectory(${UNLEASHED_RECOMP_TOOLS_ROOT}/XenosRecomp)

# XenosRecomp contains game-specific shader rewrites and specialization flags
# behind this definition. Host-tools-only builds do not visit
# UnleashedRecompLib/CMakeLists.txt, so defining it there alone silently emits
# an incomplete Switch shader cache.
target_compile_definitions(XenosRecomp PRIVATE UNLEASHED_RECOMP)
