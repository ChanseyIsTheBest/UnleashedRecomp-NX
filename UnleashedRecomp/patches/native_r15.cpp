// [Switch] Round 15: setup of the round 15 natives (one file each; the CRI ones are in audio_dsp_patches.cpp), and the
// scene-graph prefetch hooks.
#include <stdafx.h>

#if defined(__SWITCH__)

#include <kernel/memory.h>
#include <user/config.h>
#include "native_hot_patches.h"
#include "native_r15.h"

// SwitchSceneGraphPrefetch (read by the two mid-asm hooks below; set before guest code runs). The scene-graph update
// walk touches each child node for the first time right before it processes it (the game's own cache hint comes one
// instruction earlier, too late to hide the miss): 39 % of the node walk's samples and 63 % of the object loop's were
// on those first loads in the round 14 stage profile. Each hook reads the next element of the array the loop walks
// only when the loop itself reads it on its next pass, and prefetches what the loop will load from it. A prefetch
// never faults and changes no value, and nothing here is stored: the guest computes the same results.
static bool g_sceneGraphPrefetch = false;

void InitRound15Natives(uint8_t* base)
{
    InitNativePoolAllocator(base);
    InitNativePathFollowing(base);
    InitNativeMaterialAnimation(base);
    InitNativeSplineAnimation(base);
    InitNativeMoppVm(base);

    g_sceneGraphPrefetch = Config::SwitchSceneGraphPrefetch;
    if (g_sceneGraphPrefetch)
        fprintf(stderr, "[native] scene-graph walk prefetches the next child\n");
}

// The node walk's child loop: r30 points at the current child slot, r28 is the end of the slots; the loop loads the
// child from 0(r30), recurses, and goes on while r30 + 4 != r28. The recursion first reads the child's flag bytes at
// 56-61 and its vtable at 0.
void SceneGraphNodePrefetchMidAsmHook(PPCRegister& r30, PPCRegister& r28)
{
    if (!g_sceneGraphPrefetch)
        return;

    uint8_t* const base = g_memory.base;
    const uint32_t nextSlot = r30.u32 + 4;
    if (nextSlot >= r28.u32 || nextSlot < r30.u32)
        return;

    const uint32_t next = PPC_LOAD_U32(nextSlot);
    __builtin_prefetch(base + next, 0);
    __builtin_prefetch(base + uint32_t(next + 56), 0);
}

// The object loop: r27 points at a vector (begin at 4, end at 8), r29 is the index and r28 four times it; the loop
// goes on while r29 < (end - begin) >> 2 (it reads begin and end again on every pass) and loads the element at
// begin + r28, then that object's words at 4 and 0.
void SceneGraphObjectPrefetchMidAsmHook(PPCRegister& r27, PPCRegister& r28, PPCRegister& r29)
{
    if (!g_sceneGraphPrefetch)
        return;

    uint8_t* const base = g_memory.base;
    const uint32_t begin = PPC_LOAD_U32(r27.u32 + 4);
    const uint32_t end = PPC_LOAD_U32(r27.u32 + 8);
    const uint32_t count = uint32_t(int32_t(end - begin) >> 2);
    // The loop's own exits on its next pass: a null begin, then the index against the count.
    if (begin == 0 || r29.u32 + 1 >= count || r29.u32 + 1 == 0)
        return;

    const uint32_t next = PPC_LOAD_U32(begin + r28.u32 + 4);
    __builtin_prefetch(base + next, 0);
}

#else

void SceneGraphNodePrefetchMidAsmHook(PPCRegister&, PPCRegister&)
{
}

void SceneGraphObjectPrefetchMidAsmHook(PPCRegister&, PPCRegister&, PPCRegister&)
{
}

#endif
