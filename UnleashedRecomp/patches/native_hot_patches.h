#pragma once

#include <cstdint>

// [Switch] SwitchNativeMapFind, SwitchNativeQuatDecode, SwitchNativeBonePalette, SwitchNativeLayerMaskTest,
// SwitchVerifyNativeHotFunctions (patches/native_hot_patches.cpp): after the image is loaded, before guest code runs.
void InitNativeHotFunctions(uint8_t* base);

// [Switch] Round 15 natives (patches/native_r15.cpp calls each one's setup): after InitNativeHotFunctions.
void InitRound15Natives(uint8_t* base);
