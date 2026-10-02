#pragma once

#include <cstdint>

// [Switch] Round 15 natives: each file sets itself up from the loaded image (code checks, its [Switch] key) and prints
// one "[native] ..." line. Called by InitRound15Natives (native_r15.cpp) after InitNativeHotFunctions, before guest
// code runs.
void InitNativePoolAllocator(uint8_t* base);     // native_r15_pool.cpp, SwitchNativePoolAllocator
void InitNativePathFollowing(uint8_t* base);     // native_r15_localized.cpp, SwitchNativePathFollowing
void InitNativeMaterialAnimation(uint8_t* base); // native_r15_material.cpp, SwitchNativeMaterialAnimation
void InitNativeSplineAnimation(uint8_t* base);   // native_r15_localized.cpp, SwitchNativeSplineAnimation
void InitNativeMoppVm(uint8_t* base);            // native_r15_localized.cpp, SwitchNativeMoppVm

// A recompiled guest function a native calls without replacing it, named by its address in two halves:
// tools/switch-direct-calls.py takes every eight-digit 82/83 token in these sources for a hooked function (whose
// callers then keep their calls through the weak symbol), so such a helper is written GUEST_IMPL(82E0, 2C90).
#define GUEST_IMPL_JOIN(high, low) __imp__sub_##high##low
#define GUEST_IMPL(high, low) GUEST_IMPL_JOIN(high, low)
