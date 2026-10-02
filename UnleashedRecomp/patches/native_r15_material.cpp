// [Switch] Round 15, SwitchNativeMaterialAnimation: the material (UV) animation's per-channel keyframe search and lerp
// (82E46FA0) as native code.
#include <stdafx.h>

#if defined(__SWITCH__)

#include <array>
#include <bit>
#include <vector>
#include <user/config.h>
#include "native_r15.h"
#include "native_verify.h"
#include "verify_sampling.h"

// 82E46FA0 applies a material animation at a time f1: it finds the material's entry in the animation's name list, then
// for each animated channel finds the keyframe pair around the time by a linear scan down from the last key, lerps,
// and passes the value to the setter (82E46EB0, the texcoord offset). It was the hottest recompiled function of the hub
// in round 14 (2.8 % of the game thread): its scan keeps the key pointer in the context and stores it on every step
// (the loop barrier of a function with double compares), and each step converts the key to double.
//
// This does what the recompiled function does: the same guest loads in the same order (the scan only reads keys), the
// same calls with the same register state in the context (the "is ready" checks, the name compares, the setter), the
// same floating-point expressions as the generated code for the value, the flush mode switched off at the same points,
// and at the end the same registers. The scan finds the same key with the key pointer in a register: the largest
// i <= n-2 with !(t < key[i]) going down, which stops at 0 at the latest (key 0 was compared before the scan, so the
// scan is entered only when !(t < key[0])). Every key word is passed through an empty volatile asm after the channel's
// flush-mode switch, so its conversion and compare cannot be scheduled before the switch (the msr is a volatile asm
// without a memory clobber).

PPC_FUNC_IMPL(__imp__sub_82E46FA0);
PPC_FUNC_IMPL(__imp__sub_82E46EB0);
PPC_FUNC_IMPL(GUEST_IMPL(82E0, 6C40)); // "is ready" (a flag and a virtual call)
PPC_FUNC_IMPL(GUEST_IMPL(82DF, B028)); // name equality

namespace
{
    bool g_nativeMaterialAnimation = false;

    // SwitchVerifyNativeHotFunctions: the register block at every setter call, for the recompiled run and the native
    // one (the setter's own stores are not in a verify region, so its arguments are compared instead).
    constexpr size_t kRegisterBlock = offsetof(PPCContext, v13) + sizeof(PPCVRegister) - offsetof(PPCContext, r3);
    using RegisterBlock = std::array<uint8_t, kRegisterBlock>;
    thread_local std::vector<RegisterBlock>* t_setterCalls = nullptr;

    inline uint32_t PinnedLoad(uint8_t* base, uint64_t address)
    {
        uint32_t value = __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + address));
        __asm__ __volatile__("" : "+r"(value));
        return value;
    }

    inline double KeyWord(uint8_t* base, uint64_t address)
    {
        return double(std::bit_cast<float>(PinnedLoad(base, address)));
    }
}

// The setter, forwarded (its only caller is 82E46FA0): records the context at each call while verifying.
PPC_FUNC(sub_82E46EB0)
{
    if (t_setterCalls != nullptr) [[unlikely]]
    {
        RegisterBlock block;
        memcpy(block.data(), &ctx.r3, kRegisterBlock);
        t_setterCalls->push_back(block);
    }

    __imp__sub_82E46EB0(ctx, base);
}

static void NativeMaterialAnimation(PPCContext& ctx, uint8_t* base)
{
    // stwu r1,-160(r1): the back-chain word, then the frame (as the recompiled code: a 32-bit write of r1).
    const uint32_t frame = ctx.r1.u32 - 160;
    PPC_STORE_U32(frame, ctx.r1.u32);
    ctx.r1.u32 = frame;

    const uint64_t object = ctx.r3.u64;
    ctx.fpscr.disableFlushMode();
    const double time = ctx.f1.f64;
    const uint64_t animation = ctx.r4.u64;

    do
    {
        if (uint32_t(object) == 0 || uint32_t(animation) == 0)
            break;

        GUEST_IMPL(82E0, 6C40)(ctx, base);
        if ((ctx.r3.u32 & 0xFF) == 0)
            break;
        ctx.r3.u64 = animation;
        GUEST_IMPL(82E0, 6C40)(ctx, base);
        if ((ctx.r3.u32 & 0xFF) == 0)
            break;

        const uint32_t names = PPC_LOAD_U32_D(uint32_t(object), 20);
        if (names == 0)
            break;

        // The name list's entry count (8-byte entries).
        uint64_t count = 0;
        const uint32_t first = PPC_LOAD_U32_D(names, 16);
        if (first != 0)
        {
            ctx.r10.u64 = PPC_LOAD_U32_D(names, 20);
            const uint64_t bytes = ctx.r10.u64 - uint64_t(first);
            count = uint64_t(int64_t(int32_t(uint32_t(bytes)) >> 3));
        }
        if (uint32_t(count) == 0)
            break;

        // The material's name in the list.
        const uint64_t name = animation + 36;
        uint64_t index = 0;
        bool found = false;
        for (uint64_t offset = 0;; offset += 4)
        {
            ctx.r4.u64 = name;
            ctx.r3.u64 = uint64_t(PPC_LOAD_U32_D(names, 32)) + offset;
            GUEST_IMPL(82DF, B028)(ctx, base);
            if ((ctx.r3.u32 & 0xFF) != 0)
            {
                found = true;
                break;
            }
            index++;
            if (!(uint32_t(index) < uint32_t(count)))
                break;
        }
        if (!found)
            break;

        ctx.r10.u64 = uint64_t(uint32_t(index) << 3);
        const uint32_t entry = PPC_LOAD_U32(ctx.r10.u32 + PPC_LOAD_U32_D(names, 16));
        const uint64_t target = PPC_LOAD_U8_D(entry, 20);

        if (!(PPC_LOAD_U32_D(uint32_t(animation), 12) > 0))
            break;

        uint64_t channel = 0;
        for (uint64_t offset = 0;; offset += 12)
        {
            ctx.r8.u64 = offset + uint64_t(PPC_LOAD_U32_D(uint32_t(animation), 16));
            const uint32_t keys = PPC_LOAD_U32_D(ctx.r8.u32, 4);
            if (keys != 0)
            {
                ctx.r10.u64 = PPC_LOAD_U32_D(ctx.r8.u32, 8);
                ctx.fpscr.disableFlushMode();
                const uint32_t keyBase = ctx.r10.u32;
                if (time < KeyWord(base, uint64_t(keyBase)))
                {
                    // Before the first key: its value.
                    ctx.f1.f64 = KeyWord(base, uint64_t(keyBase) + 4);
                }
                else
                {
                    ctx.r9.u64 = uint64_t(keys << 3) + ctx.r10.u64;
                    if (!(time < KeyWord(base, uint32_t(ctx.r9.u32 - 8))))
                    {
                        // At or after the last key: its value.
                        ctx.f1.f64 = KeyWord(base, uint32_t(ctx.r9.u32 - 4));
                    }
                    else
                    {
                        // The scan down from key n-2 (n >= 2 here), the key pointer in a register.
                        uint64_t i = uint64_t(keys) - 2;
                        ctx.r9.u64 = uint64_t(uint32_t(i) << 3);
                        uint64_t pointer = ctx.r9.u64 + ctx.r10.u64;
                        while (time < KeyWord(base, uint32_t(pointer)))
                        {
                            pointer -= 8;
                            i -= 1;
                        }

                        ctx.r10.u64 = PPC_LOAD_U32_D(ctx.r8.u32, 8);
                        const uint32_t key = uint32_t(uint64_t(uint32_t(i) << 3) + ctx.r10.u64);
                        PPCRegister f0;
                        f0.f64 = KeyWord(base, key);
                        ctx.f13.f64 = KeyWord(base, uint64_t(key) + 8);
                        ctx.f12.f64 = double(float(time - f0.f64));
                        f0.f64 = double(float(ctx.f13.f64 - f0.f64));
                        ctx.f13.f64 = KeyWord(base, uint64_t(key) + 4);
                        ctx.f11.f64 = KeyWord(base, uint64_t(key) + 12);
                        ctx.f11.f64 = double(float(ctx.f11.f64 - ctx.f13.f64));
                        f0.f64 = double(float(ctx.f12.f64 / f0.f64));
                        ctx.f1.f64 = double(float(__builtin_fma(f0.f64, ctx.f11.f64, ctx.f13.f64)));
                    }
                }

                ctx.r4.u64 = target;
                ctx.r5.u64 = PPC_LOAD_U32_D(ctx.r8.u32, 0);
                ctx.r3.u64 = object;
                sub_82E46EB0(ctx, base);
            }

            channel++;
            if (!(uint32_t(channel) < PPC_LOAD_U32_D(uint32_t(animation), 12)))
                break;
        }
    } while (false);

    ctx.r1.s64 = ctx.r1.s64 + 160;
    ctx.fpscr.disableFlushMode();
}

namespace
{
    thread_local std::vector<RegisterBlock>* t_recordOriginal = nullptr;
    thread_local std::vector<RegisterBlock>* t_recordNative = nullptr;

    void OriginalRecorded(PPCContext& ctx, uint8_t* base)
    {
        t_setterCalls = t_recordOriginal;
        __imp__sub_82E46FA0(ctx, base);
        t_setterCalls = nullptr;
    }

    void NativeRecorded(PPCContext& ctx, uint8_t* base)
    {
        t_setterCalls = t_recordNative;
        NativeMaterialAnimation(ctx, base);
        t_setterCalls = nullptr;
    }
}

PPC_FUNC(sub_82E46FA0)
{
    if (!g_nativeMaterialAnimation)
    {
        __imp__sub_82E46FA0(ctx, base);
        return;
    }

    if (native_verify::g_verify && VerifyThisCall())
    {
        std::vector<RegisterBlock> original, native;
        t_recordOriginal = &original;
        t_recordNative = &native;
        native_verify::Verify("material animation", ctx, base, OriginalRecorded, NativeRecorded,
            { { ctx.r1.u32 - 160, 4 } });
        t_recordOriginal = t_recordNative = nullptr;
        native_verify::ReportVerify("material animation setter calls", original == native);
        return;
    }

    NativeMaterialAnimation(ctx, base);
}

void InitNativeMaterialAnimation(uint8_t* base)
{
    g_nativeMaterialAnimation = Config::SwitchNativeMaterialAnimation &&
        native_verify::CodeMatches(base, 0x82E46FA0, 0x1AC, 0xBF6B1A36949CBF7Eull);
    if (g_nativeMaterialAnimation)
        fprintf(stderr, "[native] material animation%s\n", native_verify::g_verify ? " (verifying)" : "");
}

#endif
