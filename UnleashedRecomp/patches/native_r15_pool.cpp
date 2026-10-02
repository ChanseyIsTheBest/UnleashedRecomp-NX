// [Switch] Round 15, SwitchNativePoolAllocator: the engine's small-object pool (free-list pop and push, and the free
// path's wrapper) as native code.
#include <stdafx.h>

#if defined(__SWITCH__)

#include <user/config.h>
#include "native_r15.h"
#include "native_verify.h"
#include "verify_sampling.h"

// The engine's allocator serves small blocks from per-size free lists (each with a critical section), through 4 nested
// recompiled functions per allocation and per free; in the round 14 stage profile the family was 2.3 % of the game
// thread. Each pop or push takes the list's lock through two virtual calls on the lock object, whose targets for the
// engine's critical-section class are one-instruction wrappers around RtlEnterCriticalSection and
// RtlLeaveCriticalSection.
//
// These do what the recompiled functions do, step by step: the same guest loads and stores in the same order, the
// same kernel calls with the same arguments (the wrappers' own code: r3 + 4, then the import), and at the end the
// same values in every register the recompiled code leaves in the context. A virtual call is inlined only when its
// target is the known wrapper (one load and compare per call; any other target is called as the guest would). The
// stack frame the recompiled functions build is below the caller's stack pointer once they return, and nothing reads
// it, so it is not written. Each runs only if the guest code is the code it was written from (checked at startup).

PPC_EXTERN_FUNC(__imp__RtlEnterCriticalSection);
PPC_EXTERN_FUNC(__imp__RtlLeaveCriticalSection);
PPC_FUNC_IMPL(__imp__sub_82E02BD8);
PPC_FUNC_IMPL(__imp__sub_82E02CF8);
PPC_FUNC_IMPL(__imp__sub_82E02E40);
PPC_FUNC_IMPL(GUEST_IMPL(82E0, 2C90)); // the free path's size-to-list search (a leaf), called as is

namespace
{
    bool g_nativePool = false;

    // The critical-section class's wrappers (vtable words 1 and 3), checked at startup.
    constexpr uint32_t kEnterWrapper = 0x82E0'3268;
    constexpr uint32_t kLeaveWrapper = 0x82E0'32A0;

    // The lock object's virtual call: the wrapper's own code when the target is the known one.
    inline void LockCall(PPCContext& ctx, uint8_t* base, uint32_t target)
    {
        if (target == kEnterWrapper)
        {
            ctx.r3.s64 = ctx.r3.s64 + 4;
            __imp__RtlEnterCriticalSection(ctx, base);
        }
        else if (target == kLeaveWrapper)
        {
            ctx.r3.s64 = ctx.r3.s64 + 4;
            __imp__RtlLeaveCriticalSection(ctx, base);
        }
        else
        {
            PPC_CALL_INDIRECT_FUNC(target);
        }
    }

    // The list header of a size class: free-list head at 8, lock object at 12.
    uint32_t PopList(PPCContext& ctx, uint8_t* base, uint32_t* list)
    {
        const uint32_t pool = ctx.r3.u32;
        if (PPC_LOAD_U8(pool + 36) == 0)
            return 0;
        const uint32_t size = ctx.r4.u32;
        if (size == 0 || size > PPC_LOAD_U32(pool + 16))
            return 0;
        ctx.r10.u64 = uint64_t(size << 2);
        const uint64_t slot = ctx.r10.u64 + uint64_t(PPC_LOAD_U32(pool + 0));
        *list = PPC_LOAD_U32(uint32_t(slot) - 4);
        return *list;
    }
}

// 82E02BD8: pop a block of the size class r4 from the pool r3 (0 when the pool is off, the size is out of range, the
// class has no list, or its list is empty).
static void NativePoolPop(PPCContext& ctx, uint8_t* base)
{
    uint32_t list = 0;
    if (PopList(ctx, base, &list) == 0)
    {
        ctx.r3.u64 = 0;
        return;
    }

    ctx.r3.u64 = PPC_LOAD_U32(list + 12);
    LockCall(ctx, base, PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) + 4));
    const uint32_t head = PPC_LOAD_U32(list + 8);
    uint64_t result = 0;
    if (head != 0)
    {
        ctx.r10.u64 = PPC_LOAD_U32(head);
        result = head;
        PPC_STORE_U32(list + 8, ctx.r10.u32);
    }

    ctx.r3.u64 = PPC_LOAD_U32(list + 12);
    LockCall(ctx, base, PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) + 12));
    ctx.r3.u64 = result;
}

// 82E02CF8: push the block r4 onto list r5 of the pool r3 (1; 0 for a negative list index).
static void NativePoolPush(PPCContext& ctx, uint8_t* base)
{
    const uint32_t block = ctx.r4.u32;
    if (ctx.r5.s32 < 0)
    {
        ctx.r3.u64 = 0;
        return;
    }

    const uint32_t lists = PPC_LOAD_U32(ctx.r3.u32 + 8);
    ctx.r10.u64 = uint64_t(ctx.r5.u32 << 4);
    const uint32_t list = uint32_t(ctx.r10.u64 + uint64_t(lists));
    ctx.r3.u64 = PPC_LOAD_U32(list + 12);
    LockCall(ctx, base, PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) + 4));
    PPC_STORE_U32(block, PPC_LOAD_U32(list + 8));
    ctx.r3.u64 = PPC_LOAD_U32(list + 12);
    PPC_STORE_U32(list + 8, block);
    LockCall(ctx, base, PPC_LOAD_U32(PPC_LOAD_U32(ctx.r3.u32) + 12));
    ctx.r3.u64 = 1;
}

// 82E02E40: return the block r4 to the pool r3 if the pool is on (the search for its list, then the push); 0 if off.
static void NativePoolFree(PPCContext& ctx, uint8_t* base)
{
    ctx.r7.u64 = ctx.r3.u64;
    if (PPC_LOAD_U8(ctx.r7.u32 + 36) == 0)
    {
        ctx.r3.u64 = 0;
        return;
    }

    GUEST_IMPL(82E0, 2C90)(ctx, base);
    ctx.r5.u64 = ctx.r3.u64;
    ctx.r3.u64 = ctx.r7.u64;
    NativePoolPush(ctx, base);
}

// SwitchVerifyNativeHotFunctions: both run with the list's lock held around them (it is recursive), so that no other
// thread changes the list between the recompiled run and the native one (the verify puts the memory back in between).
template<typename Native>
static void VerifyUnderLock(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original, Native native,
    uint32_t list, std::initializer_list<native_verify::Region> regions)
{
    const uint32_t lock = PPC_LOAD_U32(list + 12);
    if (PPC_LOAD_U32(PPC_LOAD_U32(lock) + 4) != kEnterWrapper || PPC_LOAD_U32(PPC_LOAD_U32(lock) + 12) != kLeaveWrapper)
    {
        original(ctx, base);
        return;
    }

    PPCContext lockCtx = ctx;
    lockCtx.r3.u64 = uint64_t(lock) + 4;
    __imp__RtlEnterCriticalSection(lockCtx, base);
    native_verify::Verify(name, ctx, base, original, native, regions);
    lockCtx.r3.u64 = uint64_t(lock) + 4;
    __imp__RtlLeaveCriticalSection(lockCtx, base);
}

PPC_FUNC(sub_82E02BD8)
{
    if (!g_nativePool)
    {
        __imp__sub_82E02BD8(ctx, base);
        return;
    }

    if (native_verify::g_verify && VerifyThisCall())
    {
        PPCContext probe = ctx;
        uint32_t list = 0;
        if (PopList(probe, base, &list) == 0)
        {
            native_verify::Verify("pool pop", ctx, base, __imp__sub_82E02BD8, NativePoolPop, {});
            return;
        }
        VerifyUnderLock("pool pop", ctx, base, __imp__sub_82E02BD8, NativePoolPop, list, { { list + 8, 4 } });
        return;
    }

    NativePoolPop(ctx, base);
}

PPC_FUNC(sub_82E02CF8)
{
    if (!g_nativePool)
    {
        __imp__sub_82E02CF8(ctx, base);
        return;
    }

    if (native_verify::g_verify && VerifyThisCall())
    {
        if (ctx.r5.s32 < 0)
        {
            native_verify::Verify("pool push", ctx, base, __imp__sub_82E02CF8, NativePoolPush, {});
            return;
        }
        const uint32_t list = uint32_t(uint64_t(ctx.r5.u32 << 4) + uint64_t(PPC_LOAD_U32(ctx.r3.u32 + 8)));
        VerifyUnderLock("pool push", ctx, base, __imp__sub_82E02CF8, NativePoolPush, list,
            { { list + 8, 4 }, { ctx.r4.u32, 4 } });
        return;
    }

    NativePoolPush(ctx, base);
}

// (In the verify mode the recompiled free wrapper runs: its push is the hook above, verified there.)
PPC_FUNC(sub_82E02E40)
{
    if (!g_nativePool || native_verify::g_verify)
        __imp__sub_82E02E40(ctx, base);
    else
        NativePoolFree(ctx, base);
}

void InitNativePoolAllocator(uint8_t* base)
{
    using native_verify::CodeMatches;
    g_nativePool = Config::SwitchNativePoolAllocator &&
        CodeMatches(base, 0x82E02BD8, 0xB8, 0x103EB9C931915A77ull) &&
        CodeMatches(base, 0x82E02CF8, 0x88, 0xDDC1F603581322FDull) &&
        CodeMatches(base, 0x82E02E40, 0x48, 0x67446D7F9D0DF760ull) &&
        CodeMatches(base, 0x82E02E40 - 0x1B0, 0x68, 0xCAF55E354BD6DF9Eull) && // the search, called as is
        CodeMatches(base, kEnterWrapper, 8, 0xD07295851BDFC116ull) &&
        CodeMatches(base, kLeaveWrapper, 8, 0xD0724D851BDF46BEull);
    if (g_nativePool)
        fprintf(stderr, "[native] pool allocator pop/push/free%s\n", native_verify::g_verify ? " (verifying)" : "");
}

#endif
