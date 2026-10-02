#pragma once

#include <cstdint>

// [Switch] The executable image's type descriptors (MSVC TypeDescriptor: the type_info vftable, a spare word, then
// the decorated name), found at startup by patches/message_dispatch.cpp. Their names are checked to be all
// different, and a type descriptor's name never changes, so two different ones never compare equal.
struct ImageTypeDescriptors
{
    const uint8_t* bits = nullptr; // one bit per 4 bytes from first
    uint32_t first = 0;
    uint32_t span = 0;             // 0 until the set is built and verified
};

extern ImageTypeDescriptors g_imageTypeDescriptors;

inline bool IsImageTypeDescriptor(uint32_t address)
{
    const uint32_t offset = address - g_imageTypeDescriptors.first;
    return offset < g_imageTypeDescriptors.span && (offset & 3) == 0 &&
        ((g_imageTypeDescriptors.bits[offset >> 5] >> ((offset >> 2) & 7)) & 1) != 0;
}

// SwitchExactTypeInfoSet: type_info== answers 0 for two different image type descriptors (misc_impl.cpp).
extern bool g_exactTypeInfoSet;

// SwitchVerifyMessageDispatch: type_info== tells the dispatcher being verified about each comparison.
extern bool g_verifyMessageDispatch;
void NoteTypeInfoComparison(uint32_t typeInfo, bool equal);

// After the image is loaded and before any guest code runs.
void InitMessageDispatch(uint8_t* base);
