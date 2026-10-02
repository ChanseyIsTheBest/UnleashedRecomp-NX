// Hot guest functions as exact native code (SwitchNativeMapFind, SwitchNativeQuatDecode, SwitchNativeBonePalette, SwitchNativeLayerMaskTest).
#include <stdafx.h>

#if defined(__SWITCH__)

#include <user/config.h>
#include <arm_neon.h>
#include "native_hot_patches.h"
#include "verify_sampling.h"
#include "native_verify.h"

// [Switch] Round 11. Small guest functions the CPU profile of the game's main thread found hot, as native code (set up
// by InitNativeHotFunctions in main(), before guest code runs). Each does what the recompiled function does, step
// by step: the same loads and stores in the same order, and at the end the same values in every register the
// recompiled code leaves in the context (volatile ones included), so nothing a caller could look at differs. The
// recompiled code keeps these values in the context on every step and spills them around each loop's barrier; the
// native code keeps them in registers. Each runs only if the guest code at its address is exactly the code it was
// written from (checked at startup), and SwitchVerifyNativeHotFunctions runs both and compares, keeping the
// recompiled code's effects.

uint32_t g_verifyEvery = 1;

// Round 15: the check and verify helpers moved to native_verify.h (the round 15 natives use them too).
namespace native_verify
{
    bool g_verify = false;

    namespace
    {
        std::atomic<uint32_t> g_mismatches;
        std::atomic<uint64_t> g_verifiedCalls;
    }

    void ReportVerify(const char* name, bool same)
    {
        const uint64_t calls = g_verifiedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!same)
        {
            const uint32_t mismatches = g_mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
            if (mismatches <= 32 || (mismatches & 1023) == 0)
                fprintf(stderr, "[native] MISMATCH %u in %s\n", mismatches, name);
        }

        if ((calls & (calls - 1)) == 0 && calls >= 1024)
        {
            fprintf(stderr, "[native] verified %llu calls, %u mismatches\n", (unsigned long long)calls,
                g_mismatches.load(std::memory_order_relaxed));
        }
    }
}

using namespace native_verify;

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeMapFind: std::map<uint32_t, T>::find (MSVC's tree: nodes with left at 0, right at 8, the key at 12 and
// the "is nil" byte at Nil), in six identical copies that differ only in Nil. The material code looks up its shader
// parameters with it (82E18B28 alone was 3.6% of the game thread). The iterator goes to [r3]; like the recompiled
// code, this leaves r8-r10 as it does and writes the two words below the stack pointer.

template<uint32_t Nil>
static void NativeMapFind(PPCContext& ctx, uint8_t* base)
{
    uint64_t r10 = PPC_LOAD_U32(ctx.r4.u32 + 4);
    uint32_t r11 = PPC_LOAD_U32(uint32_t(r10) + 4);
    uint64_t r9 = PPC_LOAD_U8(r11 + Nil);
    uint64_t r8 = ctx.r8.u64;
    if (uint32_t(r9) == 0)
    {
        r9 = PPC_LOAD_U32(ctx.r5.u32 + 0);
        const uint32_t key = uint32_t(r9);
        do
        {
            r8 = PPC_LOAD_U32(r11 + 12);
            if (uint32_t(r8) < key)
            {
                r11 = PPC_LOAD_U32(r11 + 8);
            }
            else
            {
                r10 = r11;
                r11 = PPC_LOAD_U32(r11 + 0);
            }

            r8 = PPC_LOAD_U8(r11 + Nil);
        } while (uint32_t(r8) == 0);
    }

    r11 = PPC_LOAD_U32(ctx.r4.u32 + 4);
    PPC_STORE_U32(ctx.r1.u32 + -16, uint32_t(r10));
    uint32_t result = ctx.r1.u32 + -12;
    bool end = true;
    if (uint32_t(r10) != r11)
    {
        r10 = PPC_LOAD_U32(uint32_t(r10) + 12);
        r9 = PPC_LOAD_U32(ctx.r5.u32 + 0);
        if (!(uint32_t(r9) < uint32_t(r10)))
        {
            result = ctx.r1.u32 + -16;
            end = false;
        }
    }

    if (end)
        PPC_STORE_U32(ctx.r1.u32 + -12, r11);

    PPC_STORE_U32(ctx.r3.u32 + 0, PPC_LOAD_U32(result));
    ctx.r8.u64 = r8;
    ctx.r9.u64 = r9;
    ctx.r10.u64 = r10;
}

namespace
{
    struct MapFindHook
    {
        uint32_t address;
        uint64_t codeHash;
        bool active;
    };

    MapFindHook g_mapFinds[] =
    {
        { 0x82E18B28, 0x9AAF804E165E57CAull, false },
        { 0x82870CB0, 0x9AAF804E165E57CAull, false },
        { 0x8252C038, 0x7C3C5179706D89E2ull, false },
        { 0x82E9DA70, 0x7C3C5179706D89E2ull, false },
        { 0x82E9C7E8, 0x49F69ECA29CF4302ull, false },
        { 0x82E20988, 0xCFDEEE4C743A73BAull, false },
    };
}

template<size_t Index, uint32_t Nil>
static void RunMapFind(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    if (!g_mapFinds[Index].active)
    {
        original(ctx, base);
        return;
    }

    if (g_verify && VerifyThisCall())
    {
        Verify("map find", ctx, base, original, NativeMapFind<Nil>, { { ctx.r3.u32, 4 }, { ctx.r1.u32 - 16, 8 } });
        return;
    }

    NativeMapFind<Nil>(ctx, base);
}

PPC_FUNC_IMPL(__imp__sub_82E18B28);
PPC_FUNC(sub_82E18B28) { RunMapFind<0, 21>(ctx, base, __imp__sub_82E18B28); }
PPC_FUNC_IMPL(__imp__sub_82870CB0);
PPC_FUNC(sub_82870CB0) { RunMapFind<1, 21>(ctx, base, __imp__sub_82870CB0); }
PPC_FUNC_IMPL(__imp__sub_8252C038);
PPC_FUNC(sub_8252C038) { RunMapFind<2, 25>(ctx, base, __imp__sub_8252C038); }
PPC_FUNC_IMPL(__imp__sub_82E9DA70);
PPC_FUNC(sub_82E9DA70) { RunMapFind<3, 25>(ctx, base, __imp__sub_82E9DA70); }
PPC_FUNC_IMPL(__imp__sub_82E9C7E8);
PPC_FUNC(sub_82E9C7E8) { RunMapFind<4, 33>(ctx, base, __imp__sub_82E9C7E8); }
PPC_FUNC_IMPL(__imp__sub_82E20988);
PPC_FUNC(sub_82E20988) { RunMapFind<5, 45>(ctx, base, __imp__sub_82E20988); }

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeLayerMaskTest: 82E26688, whether an object is visible in a render layer: two early outs, then whether
// the eight 64-bit words of its layer mask and the layer's share a bit. Byte order does not change which bits two
// words share, so the words are ANDed as they are in memory and only the values the context keeps are swapped.

static void NativeLayerMaskTest(PPCContext& ctx, uint8_t* base)
{
    uint32_t r11 = PPC_LOAD_U32(ctx.r3.u32 + 68);
    if (r11 != 0 && PPC_LOAD_U32(r11 + 8) == 2)
    {
        ctx.r3.s64 = 1;
        return;
    }

    r11 = PPC_LOAD_U32(ctx.r3.u32 + 16);
    if (r11 != 0)
    {
        ctx.r10.u64 = PPC_LOAD_U32(ctx.r3.u32 + 20);
        const int64_t difference = ctx.r10.s64 - int64_t(uint64_t(r11));
        const int32_t count = int32_t(uint32_t(difference)) >> 3;
        if (uint32_t(count) >= 512)
        {
            ctx.r3.s64 = 1;
            return;
        }
    }

    uint64_t r10 = uint64_t((ctx.r4.u32 << 6) & 0xFFFFFFC0u) + ctx.r3.u64 + 80;
    uint64_t r9 = ctx.r3.u64 + 272;
    uint64_t any = 0, a = 0, b = 0;
    for (uint32_t i = 0; i < 8; i++)
    {
        memcpy(&b, base + uint32_t(r9), sizeof(b));
        memcpy(&a, base + uint32_t(r10), sizeof(a));
        r10 += 8;
        r9 += 8;
        any |= a & b;
    }

    ctx.r6.u64 = __builtin_bswap64(a);
    ctx.r7.u64 = __builtin_bswap64(a & b);
    ctx.r8.u64 = __builtin_bswap64(any);
    ctx.r9.u64 = r9;
    ctx.r10.u64 = r10;
    ctx.r3.u64 = any != 0 ? 1 : 0;
}

static bool g_nativeLayerMaskTest = false;

PPC_FUNC_IMPL(__imp__sub_82E26688);
PPC_FUNC(sub_82E26688)
{
    if (!g_nativeLayerMaskTest)
        __imp__sub_82E26688(ctx, base);
    else if (g_verify && VerifyThisCall())
        Verify("layer mask test", ctx, base, __imp__sub_82E26688, NativeLayerMaskTest, {});
    else
        NativeLayerMaskTest(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeQuatDecode: 82FCFA08, an animation track's rotation decoder: a quaternion packed in 5 bytes (three
// 12-bit components and the index and sign of the dropped one) to 4 floats at r4. The integer steps are the
// recompiled code's own operations on locals instead of the context. The floating-point steps are the instructions
// GCC made of the recompiled code (the three multiply-adds fused), as one asm block with the same operands in the
// same roles (which decides NaN propagation), so the compiler cannot combine or reorder them differently. Their
// inputs are the same in all four lanes from the square root on (the constants 1.0 and 0.5 are, checked on each
// call), so those steps are done once and copied: the same correctly rounded result in each lane.

static void NativeQuatDecode(PPCContext& ctx, uint8_t* base)
{
    PPCRegister r3, r5, r6, r7, r8, r9, r10, r11, temp;
    PPCVRegister v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13;
    const uint32_t source = ctx.r3.u32;

    r11.s64 = 16;
    temp.u32 = source;
    simde_mm_store_si128((simde__m128i*)v0.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + (temp.u32 & ~0xF))), simde_mm_load_si128((simde__m128i*)&VectorMaskL[(temp.u32 & 0xF) * 16])));
    r10.s64 = -2112684032;
    simde_mm_store_si128((simde__m128i*)v13.u32, simde_mm_set1_epi32(int(0x4)));
    simde_mm_store_si128((simde__m128i*)v12.u32, simde_mm_set1_epi32(int(0x0)));
    r9.s64 = -2112684032;
    r8.s64 = r10.s64 + 31488;
    r7.u64 = PPC_LOAD_U8(source + 4);
    r6.s64 = r9.s64 + 31472;
    temp.u32 = r11.u32 + source;
    simde_mm_store_si128((simde__m128i*)v11.u8, temp.u32 & 0xF ? simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + (temp.u32 & ~0xF))), simde_mm_load_si128((simde__m128i*)&VectorMaskR[(temp.u32 & 0xF) * 16])) : simde_mm_setzero_si128());
    r5.s64 = -2112684032;
    simde_mm_store_si128((simde__m128i*)v0.u8, simde_mm_or_si128(simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v11.u8)));
    simde_mm_store_si128((simde__m128i*)v10.u32, simde_mm_unpackhi_epi32(simde_mm_load_si128((simde__m128i*)v13.u32), simde_mm_load_si128((simde__m128i*)v12.u32)));
    r3.s64 = -2112684032;
    simde_mm_store_si128((simde__m128i*)v7.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r8.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    r11.s64 = r5.s64 + 31456;
    simde_mm_store_si128((simde__m128i*)v13.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r6.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    r10.s64 = r3.s64 + 31440;
    simde_mm_store_si128((simde__m128i*)v9.u8, simde_mm_perm_epi8_(simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v7.u8)));
    r9.s64 = -2112749568;
    r8.s64 = -2112749568;
    r6.s64 = r9.s64 + 4512;
    simde_mm_store_si128((simde__m128i*)v0.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r11.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    r5.s64 = r8.s64 + 4608;
    v8.u32[0] = v9.u32[0] >> (v10.u8[0] & 0x1F);
    v8.u32[1] = v9.u32[1] >> (v10.u8[4] & 0x1F);
    v8.u32[2] = v9.u32[2] >> (v10.u8[8] & 0x1F);
    v8.u32[3] = v9.u32[3] >> (v10.u8[12] & 0x1F);
    simde_mm_store_si128((simde__m128i*)v11.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r10.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    r11.u64 = __builtin_rotateleft64(r7.u32 | (r7.u64 << 32), 28) & 0x3;
    r10.u64 = __builtin_rotateleft64(r7.u32 | (r7.u64 << 32), 26) & 0x1;
    simde_mm_store_si128((simde__m128i*)v10.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r6.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    simde_mm_store_si128((simde__m128i*)v7.u8, simde_mm_and_si128(simde_mm_load_si128((simde__m128i*)v8.u8), simde_mm_load_si128((simde__m128i*)v13.u8)));
    simde_mm_store_si128((simde__m128i*)v12.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r5.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));

    // vcfux v6,v7,0 (flush mode first, as the recompiled code), then the floating-point steps.
    ctx.fpscr.enableFlushMode();
    simde_mm_store_ps(v6.f32, simde_mm_cvtepu32_ps_(simde_mm_load_si128((simde__m128i*)v7.u32)));

    const float32x4_t f = vld1q_f32(v6.f32);
    const float32x4_t scale = vld1q_f32(v11.f32);
    const float32x4_t offset = vld1q_f32(v0.f32);
    const float32x4_t one = vld1q_f32(v10.f32);
    const float32x4_t half = vld1q_f32(v12.f32);
    const uint32x4_t dotMask = { 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu }; // vmsum3fp: w (lane 0 here) left out
    const uint32x4_t sign = vdupq_n_u32(0x80000000u);
    float32x4_t q13, q5, q4, q3, q2, q1, q0, p;

    // vmaddfp v13,v6,v11,v0 (fmla: v0 + v6*v11); vmsum3fp128 v5,v13,v13 (fmul, and, faddp, faddp, dup);
    // vsubfp v4,v10,v5.
    __asm__ __volatile__(
        "mov   %[q13].16b, %[offset].16b\n\t"
        "fmla  %[q13].4s, %[f].4s, %[scale].4s\n\t"
        "fmul  %[p].4s, %[q13].4s, %[q13].4s\n\t"
        "and   %[p].16b, %[p].16b, %[mask].16b\n\t"
        "faddp %[p].4s, %[p].4s, %[p].4s\n\t"
        "faddp %[p].4s, %[p].4s, %[p].4s\n\t"
        "dup   %[q5].4s, %[p].s[0]\n\t"
        "fsub  %[q4].4s, %[one].4s, %[q5].4s\n\t"
        : [q13] "=&w"(q13), [p] "=&w"(p), [q5] "=&w"(q5), [q4] "=&w"(q4)
        : [offset] "w"(offset), [f] "w"(f), [scale] "w"(scale), [mask] "w"(dotMask), [one] "w"(one));

    const uint32x4_t oneBits = vreinterpretq_u32_f32(one);
    const uint32x4_t halfBits = vreinterpretq_u32_f32(half);
    const bool uniform = vminvq_u32(vandq_u32(vceqq_u32(oneBits, vdupq_laneq_u32(oneBits, 0)),
        vceqq_u32(halfBits, vdupq_laneq_u32(halfBits, 0)))) != 0;

    // vrsqrtefp v0,v4 (fsqrt, then 1.0/that); vmulfp128 v3,v4,v12; vmulfp128 v2,v0,v0; vnmsubfp v1,v3,v2,v12
    // (-(fma: -v12 + v3*v2)); vmaddfp v31,v0,v1,v0 (fma: v0 + v0*v1); vmulfp128 v0,v4,v31.
    if (uniform)
    {
        float s4 = vgetq_lane_f32(q4, 0), sHalf = vgetq_lane_f32(half, 0);
        float s0, s3, s2, s1, sw, root, negativeHalf;
        uint32_t fmaBits;
        __asm__ __volatile__(
            "fsqrt %s[root], %s[q4]\n\t"
            "fmul  %s[q3], %s[q4], %s[half]\n\t"
            "fneg  %s[nh], %s[half]\n\t"
            "fmov  %s[q0], #1.0\n\t"
            "fdiv  %s[q0], %s[q0], %s[root]\n\t"
            "fmul  %s[q2], %s[q0], %s[q0]\n\t"
            "fmadd %s[nh], %s[q3], %s[q2], %s[nh]\n\t"
            "fmov  %w[tmp], %s[nh]\n\t"
            : [root] "=&w"(root), [q3] "=&w"(s3), [nh] "=&w"(negativeHalf), [q0] "=&w"(s0), [q2] "=&w"(s2),
              [tmp] "=&r"(fmaBits)
            : [q4] "w"(s4), [half] "w"(sHalf));
        // eor with the sign bit, then fmla v31 = v0 + v0*v1 and fmul w = v31*v4.
        s1 = std::bit_cast<float>(fmaBits ^ 0x80000000u);
        float s31;
        __asm__ __volatile__(
            "fmov  %s[s31], %s[q0]\n\t"
            "fmadd %s[s31], %s[q0], %s[q1], %s[s31]\n\t"
            "fmul  %s[w], %s[s31], %s[q4]\n\t"
            : [s31] "=&w"(s31), [w] "=&w"(sw)
            : [q0] "w"(s0), [q1] "w"(s1), [q4] "w"(s4));
        q3 = vdupq_n_f32(s3);
        q0 = vdupq_n_f32(sw);
        q2 = vdupq_n_f32(s2);
        q1 = vdupq_n_f32(s1);
    }
    else
    {
        float32x4_t root, negativeHalf, reciprocal, q31;
        __asm__ __volatile__(
            "fsqrt %[root].4s, %[q4].4s\n\t"
            "fmul  %[q3].4s, %[q4].4s, %[half].4s\n\t"
            "fneg  %[nh].4s, %[half].4s\n\t"
            "fmov  %[rc].4s, #1.0\n\t"
            "fdiv  %[rc].4s, %[rc].4s, %[root].4s\n\t"
            "fmul  %[q2].4s, %[rc].4s, %[rc].4s\n\t"
            "fmla  %[nh].4s, %[q3].4s, %[q2].4s\n\t"
            "eor   %[q1].16b, %[nh].16b, %[sign].16b\n\t"
            "mov   %[q31].16b, %[rc].16b\n\t"
            "fmla  %[q31].4s, %[rc].4s, %[q1].4s\n\t"
            "fmul  %[q0].4s, %[q31].4s, %[q4].4s\n\t"
            : [root] "=&w"(root), [q3] "=&w"(q3), [nh] "=&w"(negativeHalf), [rc] "=&w"(reciprocal), [q2] "=&w"(q2),
              [q1] "=&w"(q1), [q31] "=&w"(q31), [q0] "=&w"(q0)
            : [q4] "w"(q4), [half] "w"(half), [sign] "w"(sign));
    }

    vst1q_f32(v13.f32, q13);
    vst1q_f32(v5.f32, q5);
    vst1q_f32(v4.f32, q4);
    vst1q_f32(v3.f32, q3);
    vst1q_f32(v2.f32, q2);
    vst1q_f32(v1.f32, q1);
    vst1q_f32(v0.f32, q0);

    if (r11.u32 < 1)
        simde_mm_store_si128((simde__m128i*)v13.u32, simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i*)v13.u32), 0x39));
    else if (r11.u32 == 1)
        simde_mm_store_si128((simde__m128i*)v13.u32, simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i*)v13.u32), 0xC9));
    else if (r11.u32 < 3)
        simde_mm_store_si128((simde__m128i*)v13.u32, simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i*)v13.u32), 0xE1));

    r9.s64 = -2112684032;
    simde_mm_store_si128((simde__m128i*)v12.u8, simde_mm_load_si128((simde__m128i*)v0.u8));
    r8.s64 = -2112684032;
    r7.s64 = r9.s64 + 31376;
    r6.s64 = r8.s64 + 31344;
    r5.u64 = __builtin_rotateleft64(r11.u32 | (r11.u64 << 32), 4) & 0xFFFFFFF0;
    r3.u64 = __builtin_rotateleft64(r10.u32 | (r10.u64 << 32), 4) & 0xFFFFFFF0;
    simde_mm_store_si128((simde__m128i*)v0.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r5.u32 + r7.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    simde_mm_store_si128((simde__m128i*)v11.u8, simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)(base + ((r3.u32 + r6.u32) & ~0xF))), simde_mm_load_si128((simde__m128i*)VectorMaskL)));
    simde_mm_store_si128((simde__m128i*)v10.u8, simde_mm_or_si128(simde_mm_andnot_si128(simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v13.u8)), simde_mm_and_si128(simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v12.u8))));
    simde_mm_store_si128((simde__m128i*)v9.u8, simde_mm_and_si128(simde_mm_load_si128((simde__m128i*)v0.u8), simde_mm_load_si128((simde__m128i*)v11.u8)));
    simde_mm_store_si128((simde__m128i*)v8.u8, simde_mm_xor_si128(simde_mm_load_si128((simde__m128i*)v10.u8), simde_mm_load_si128((simde__m128i*)v9.u8)));
    simde_mm_store_si128((simde__m128i*)(base + ((ctx.r4.u32) & ~0xF)), simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i*)v8.u8), simde_mm_load_si128((simde__m128i*)VectorMaskL)));

    ctx.r3 = r3;
    ctx.r5 = r5;
    ctx.r6 = r6;
    ctx.r7 = r7;
    ctx.r8 = r8;
    ctx.r9 = r9;
    ctx.r10 = r10;
    ctx.v0 = v0;
    ctx.v1 = v1;
    ctx.v2 = v2;
    ctx.v3 = v3;
    ctx.v4 = v4;
    ctx.v5 = v5;
    ctx.v6 = v6;
    ctx.v7 = v7;
    ctx.v8 = v8;
    ctx.v9 = v9;
    ctx.v10 = v10;
    ctx.v11 = v11;
    ctx.v12 = v12;
    ctx.v13 = v13;
}

static bool g_nativeQuatDecode = false;

PPC_FUNC_IMPL(__imp__sub_82FCFA08);
PPC_FUNC(sub_82FCFA08)
{
    if (!g_nativeQuatDecode)
        __imp__sub_82FCFA08(ctx, base);
    else if (g_verify && VerifyThisCall())
        Verify("quaternion decoder", ctx, base, __imp__sub_82FCFA08, NativeQuatDecode, { { ctx.r4.u32 & ~0xFu, 16 } });
    else
        NativeQuatDecode(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeBonePalette: 82E18118, the upload of a mesh's bone matrices into the vertex shader constants: for each
// bone, three rows of its transposed 4x4 matrix into consecutive registers, and the matching bits into the device's
// dirty word, four bones per pass. No arithmetic on the values: 32-bit words move as they are. Per group of four,
// the same order as the recompiled code: the index bytes and the dirty word read, the 16 rows read, then the dirty
// word and each bone's three registers written in turn. The bones left over (fewer than four) go through the
// recompiled code's own steps, the frame, the stack block and the call of the constant setter included. Only for a
// 16-byte aligned palette (lvlx then loads whole rows); anything else runs the recompiled function.

// srad of 1 << 63 then srd, with the recompiled code's handling of shifts of 64 or more.
static uint64_t DirtyMask(uint64_t arithmeticShift, uint64_t logicalShift)
{
    uint64_t amount = arithmeticShift & 0x7F;
    if (amount > 0x3F)
        amount = 0x3F;

    const uint64_t mask = uint64_t(int64_t(0x8000000000000000ull) >> amount);
    return (logicalShift & 0x40) ? 0 : (mask >> (logicalShift & 0x7F));
}

static void NativeBonePalette(PPCContext& ctx, uint8_t* base)
{
    const uint32_t frame = -256 + ctx.r1.u32;
    PPC_STORE_U32(frame, ctx.r1.u32);
    ctx.r1.u32 = frame;

    const uint64_t r22 = ctx.r7.u64;
    const uint64_t r31 = ctx.r3.u64;
    const uint64_t r26 = ctx.r5.u64;
    const uint64_t r23 = ctx.r6.u64;
    ctx.r10.s64 = 1;
    uint64_t r11 = r22 & 0xFFFFFFFC;
    uint64_t r24 = 0;
    if (int32_t(uint32_t(r11)) != 0)
    {
        r11 = uint64_t(uint32_t(r11 - 1) >> 2);
        const uint64_t firstRegister = (uint32_t(ctx.r4.u64 + 125) << 4) & 0xFFFFFFF0;
        uint64_t groups = r11 + 1;
        uint64_t r9 = r23 + 2;
        uint64_t r10 = ctx.r4.u64 + 8;
        uint64_t destination = firstRegister + r31;
        r24 = uint32_t(groups << 2);
        uint64_t r8 = 0;

        do
        {
            const uint32_t index0 = PPC_LOAD_U8(uint32_t(r9) + -2);
            const uint32_t index1 = PPC_LOAD_U8(uint32_t(r9) + -1);
            const uint32_t index2 = PPC_LOAD_U8(uint32_t(r9) + 0);
            const uint32_t index3 = PPC_LOAD_U8(uint32_t(r9) + 1);
            uint64_t dirty = PPC_LOAD_U64(uint32_t(r31) + 0);

            // The 16 rows, as the recompiled code reads them: bone 0 then 1, 2 and 3.
            uint32_t rows[4][4][4];
            const uint32_t palette[4] =
            {
                uint32_t((index0 << 6) + r26), uint32_t((index1 << 6) + r26),
                uint32_t((index2 << 6) + r26), uint32_t((index3 << 6) + r26),
            };
            for (uint32_t bone = 0; bone < 4; bone++)
            {
                for (uint32_t row = 0; row < 4; row++)
                    memcpy(rows[bone][row], base + uint32_t(palette[bone] + row * 16), 16);
            }

            // Bone b's registers get columns 0-2 of its matrix, and the registers of bone b set their dirty bits.
            auto columns = [&](uint32_t bone, uint32_t address, uint32_t column)
                {
                    uint32_t words[4] = { rows[bone][0][column], rows[bone][1][column], rows[bone][2][column], rows[bone][3][column] };
                    memcpy(base + (address & ~0xFu), words, 16);
                };

            const uint32_t first = uint32_t(destination);
            auto mask = [&](uint64_t low, uint64_t high)
                {
                    const uint64_t lowWord = uint32_t(low) >> 2;
                    const uint64_t highWord = uint32_t(high) >> 2;
                    return DirtyMask((highWord - lowWord) & 0xFFFFFFFF, lowWord);
                };

            PPC_STORE_U64(uint32_t(r31) + 0, mask(r10 - 8, r10 - 6) | dirty);
            columns(0, first - 64, 1);
            columns(0, first - 80, 0);
            columns(0, first - 48, 2);

            r8 = mask(r10 - 5, r10 - 3) | PPC_LOAD_U64(uint32_t(r31) + 0);
            PPC_STORE_U64(uint32_t(r31) + 0, r8);
            columns(1, first - 32, 0);
            columns(1, first - 16, 1);
            columns(1, first + 0, 2);

            r8 = mask(r10 - 2, r10) | PPC_LOAD_U64(uint32_t(r31) + 0);
            PPC_STORE_U64(uint32_t(r31) + 0, r8);
            columns(2, first + 16, 0);
            columns(2, first + 32, 1);
            columns(2, first + 48, 2);

            r8 = mask(r10 + 1, r10 + 3) | PPC_LOAD_U64(uint32_t(r31) + 0);
            PPC_STORE_U64(uint32_t(r31) + 0, r8);
            columns(3, first + 64, 0);
            columns(3, first + 80, 1);
            columns(3, first + 96, 2);

            groups--;
            r9 += 4;
            destination += 192;
            r10 += 12;
        } while (int32_t(uint32_t(groups)) != 0);

        // What the last pass leaves in the context.
        ctx.r3.s64 = 80;
        ctx.r5.u64 = groups;
        ctx.r6.s64 = 64;
        ctx.r7.s64 = 96;
        ctx.r8.u64 = r8;
        ctx.r9.u64 = r9;
        ctx.r10.u64 = r10;
    }

    // The bones left over: the recompiled code's steps.
    if (uint32_t(r24) < uint32_t(r22))
    {
        uint64_t r25 = r24 * 3 + ctx.r4.u64;
        do
        {
            uint64_t r11b = PPC_LOAD_U8(uint32_t(r24) + uint32_t(r23));
            ctx.r10.s64 = ctx.r1.s64 + 80;
            ctx.r9.s64 = ctx.r1.s64 + 96;
            r11b = __builtin_rotateleft32(uint32_t(r11b), 6);
            ctx.r8.s64 = ctx.r1.s64 + 112;
            r11b = r11b + r26;
            const uint64_t r21 = ctx.r1.s64 + 128;
            ctx.r7.s64 = int64_t(r25) + 2;
            ctx.r4.u64 = uint32_t(r25) >> 2;
            ctx.r7.u64 = uint32_t(ctx.r7.u64) >> 2;

            // lvx128 of the four rows (aligned: the low bits of the address are ignored), then the transposes.
            uint32_t rows[4][4];
            for (uint32_t row = 0; row < 4; row++)
                memcpy(rows[row], base + (uint32_t(r11b + row * 16) & ~0xFu), 16);

            ctx.r6.s64 = 3;
            ctx.r7.s64 = ctx.r7.s64 - ctx.r4.s64;
            const uint64_t shift = ctx.r7.u64 & 0xFFFFFFFF;
            ctx.r5.s64 = ctx.r1.s64 + 80;
            ctx.r7.u64 = DirtyMask(shift, ctx.r4.u64);
            ctx.r4.u64 = r25;
            ctx.r3.u64 = r31;
            for (uint32_t column = 0; column < 4; column++)
            {
                uint32_t words[4] = { rows[0][column], rows[1][column], rows[2][column], rows[3][column] };
                const uint32_t address = column == 0 ? ctx.r10.u32 : column == 1 ? ctx.r9.u32 : column == 2 ? ctx.r8.u32 : uint32_t(r21);
                memcpy(base + (address & ~0xFu), words, 16);
            }

            sub_82BDFAA0(ctx, base);
            r24 = r24 + 1;
            r25 = r25 + 3;
        } while (uint32_t(r24) < uint32_t(r22));
    }

    ctx.r1.s64 = ctx.r1.s64 + 256;
}

static bool g_nativeBonePalette = false;

PPC_FUNC_IMPL(__imp__sub_82E18118);
PPC_FUNC(sub_82E18118)
{
    if (!g_nativeBonePalette || (ctx.r5.u32 & 0xF) != 0)
    {
        __imp__sub_82E18118(ctx, base);
        return;
    }

    if (g_verify && VerifyThisCall())
    {
        // The registers it writes, the dirty word and its frame.
        const uint32_t count = ctx.r7.u32;
        if (count <= 256)
        {
            const uint32_t first = (ctx.r3.u32 + ((ctx.r4.u32 + 120) << 4)) & ~0xFu;
            Verify("bone palette", ctx, base, __imp__sub_82E18118, NativeBonePalette,
                { { ctx.r3.u32, 8 }, { first, count * 48 + 16 }, { ctx.r1.u32 - 256, 256 } });
            return;
        }
    }

    NativeBonePalette(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// Round 12 (frame dips).
//
// SwitchFastResourceWaits: the game's waits for a resource that is still loading, five copies of one loop
//     while (!ready(resource)) { CDatabaseLoader::Update(loader); Sleep(5); }
// (82BB62C0, which the game's "make sure it is loaded" helper and its 86 callers use, 824EBE98, 826D69C8, 82A2FB48,
// 82E0C550).
// The main thread moves the database loader's queues on in that Update, so an object or effect whose data is not
// ready yet stalls the frame up to 5 ms per loading step past the moment it is ready. The hooks run the recompiled
// loops as they are and mark the thread while they run; KeDelayExecutionThread (kernel/imports.cpp) then sleeps
// 0.5 ms for their Sleep(5). The guest sees the same calls, registers and memory, only fewer milliseconds.

extern thread_local bool t_fastResourceWait;
static bool g_fastResourceWaits = false;

static void RunResourceWait(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    if (!g_fastResourceWaits || t_fastResourceWait)
    {
        original(ctx, base);
        return;
    }

    t_fastResourceWait = true;
    original(ctx, base);
    t_fastResourceWait = false;
}

PPC_FUNC_IMPL(__imp__sub_82BB62C0);
PPC_FUNC(sub_82BB62C0) { RunResourceWait(ctx, base, __imp__sub_82BB62C0); }
PPC_FUNC_IMPL(__imp__sub_824EBE98);
PPC_FUNC(sub_824EBE98) { RunResourceWait(ctx, base, __imp__sub_824EBE98); }
PPC_FUNC_IMPL(__imp__sub_826D69C8);
PPC_FUNC(sub_826D69C8) { RunResourceWait(ctx, base, __imp__sub_826D69C8); }
PPC_FUNC_IMPL(__imp__sub_82A2FB48);
PPC_FUNC(sub_82A2FB48) { RunResourceWait(ctx, base, __imp__sub_82A2FB48); }
PPC_FUNC_IMPL(__imp__sub_82E0C550);
PPC_FUNC(sub_82E0C550) { RunResourceWait(ctx, base, __imp__sub_82E0C550); }

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeVisibilityTest: 82E26498, whether a mesh of a render group is drawn in a pass (the render walk asks it
// for every mesh it walks, after the layer mask test). It looks the pass up in the instance's override list (a leaf it
// calls: the word at index r4 of the 8-byte entries in [r3+80, r3+84)), then tests one bit in each of two bit
// arrays. The recompiled code of both with the call folded in: the same loads in the same order, the back-chain word
// the stwu writes, and in r3, r4 and r6-r10 what the two functions leave there.

// Round 13, SwitchRenderWalkPrefetch: the render walk asks this for the 24-byte entries of a render group one
// after the other, and each answer waits for two cache misses, the mesh's word +32 (half of this function's samples)
// and the pass override list. For the entry two ahead, both lines are prefetched now: its mesh pointer (+8) and its
// override object (+4) are read only when they lie in the 4 KB page of the entry being tested (a page the walk is
// reading, so mapped); past the group's end they are some other data, and a prefetch of any address is harmless.
// Nothing is written and no value read here reaches the answer.
static bool g_renderWalkPrefetch = false;

static void PrefetchVisibilityTestAhead(uint8_t* base, uint32_t entry)
{
    const uint32_t ahead = entry + 2 * 24;
    if (((ahead + 11) & ~0xFFFu) != (entry & ~0xFFFu))
        return;

    const uint32_t overrides = PPC_LOAD_U32(ahead + 4);
    const uint32_t mesh = PPC_LOAD_U32(ahead + 8);
    __builtin_prefetch(base + uint32_t(mesh + 32));
    __builtin_prefetch(base + uint32_t(overrides + 80));
}

static void NativeVisibilityTest(PPCContext& ctx, uint8_t* base)
{
    const uint32_t sp = ctx.r1.u32;
    PPC_STORE_U32(sp - 112, sp);

    const uint64_t r30 = ctx.r3.u64;
    const uint64_t r31 = ctx.r4.u64;
    if (g_renderWalkPrefetch)
        PrefetchVisibilityTestAhead(base, uint32_t(r30));
    uint64_t r11 = PPC_LOAD_U32_D(uint32_t(r30), 8);
    uint64_t r3 = PPC_LOAD_U32_D(uint32_t(r30), 4);
    const uint64_t r4 = PPC_LOAD_U32_D(uint32_t(r11), 32);
    ctx.r4.u64 = r4;

    // The leaf (r3, r4): leaves r3 and r10.
    uint64_t r10 = ctx.r10.u64;
    if (int32_t(uint32_t(r4)) < 0)
    {
        r3 = 0;
    }
    else
    {
        const uint64_t first = PPC_LOAD_U32_D(uint32_t(r3), 80);
        if (uint32_t(first) == 0)
        {
            r3 = 0;
        }
        else
        {
            r10 = PPC_LOAD_U32_D(uint32_t(r3), 84);
            const int64_t difference = int64_t(r10) - int64_t(first);
            const uint32_t count = uint32_t(int32_t(uint32_t(difference)) >> 3);
            if (count <= uint32_t(r4))
            {
                r3 = 0;
            }
            else
            {
                const uint64_t entries = PPC_LOAD_U32_D(uint32_t(r3), 80);
                r10 = (uint32_t(r4) << 3) & 0xFFFFFFF8u;
                r3 = PPC_LOAD_U32(uint32_t(entries) + uint32_t(r10));
            }
        }
    }

    if (uint32_t(r3) != 0)
    {
        r11 = PPC_LOAD_U32_D(uint32_t(r3), 8);
        if (uint32_t(r11) == 1 || uint32_t(r11) == 2)
        {
            ctx.r3.u64 = uint32_t(r11) == 2 ? 1 : 0;
            ctx.r10.u64 = r10;
            return;
        }
    }

    // No override for the pass.
    r10 = PPC_LOAD_U32_D(uint32_t(r30), 4);
    r11 = PPC_LOAD_U32_D(uint32_t(r10), 164);
    uint64_t r6 = ctx.r6.u64;
    uint64_t r7;
    uint64_t r8 = ctx.r8.u64;
    uint64_t r9 = ctx.r9.u64;
    if (uint32_t(r11) < 512)
    {
        r8 = PPC_LOAD_U32_D(uint32_t(r31), 24);
        r7 = r11 & 0x3F;
        r9 = uint32_t(r11) >> 6;
        r6 = PPC_LOAD_U32_D(uint32_t(r10), 160);
        uint64_t index = (uint32_t(r8) << 3) & 0xFFFFFFF8u;
        index = r9 + index + 10;
        index = (uint32_t(index) << 3) & 0xFFFFFFF8u;
        const uint64_t word = PPC_LOAD_U64(uint32_t(index) + uint32_t(r6));
        r7 = uint32_t((r7 & 0x40) ? 0 : (word >> (r7 & 0x7F))) & 0x1;
    }
    else
    {
        r7 = 1;
    }

    // The second bit array.
    r11 = PPC_LOAD_U8_D(uint32_t(r30), 14);
    if (uint32_t(r11) < 128)
    {
        r8 = PPC_LOAD_U32_D(uint32_t(r31), 24);
        r9 = uint32_t(r11) >> 6;
        r6 = r11 & 0x3F;
        uint64_t index = r8 + 7;
        index = (uint32_t(index) << 1) & 0xFFFFFFFEu;
        index = index + r9;
        index = (uint32_t(index) << 3) & 0xFFFFFFF8u;
        const uint64_t word = PPC_LOAD_U64(uint32_t(index) + uint32_t(r10));
        r11 = uint32_t((r6 & 0x40) ? 0 : (word >> (r6 & 0x7F))) & 0x1;
    }
    else
    {
        r11 = 1;
    }

    // Both bits set.
    ctx.r10.u64 = uint32_t(r7) & 0xFF;
    ctx.r3.u64 = (uint32_t(ctx.r10.u64) != 0 && (uint32_t(r11) & 0xFF) != 0) ? 1 : 0;
    ctx.r6.u64 = r6;
    ctx.r7.u64 = r7;
    ctx.r8.u64 = r8;
    ctx.r9.u64 = r9;
}

static bool g_nativeVisibilityTest = false;

PPC_FUNC_IMPL(__imp__sub_82E26498);
PPC_FUNC(sub_82E26498)
{
    if (!g_nativeVisibilityTest)
        __imp__sub_82E26498(ctx, base);
    else if (g_verify && VerifyThisCall())
        Verify("visibility test", ctx, base, __imp__sub_82E26498, NativeVisibilityTest, { { ctx.r1.u32 - 112, 4 } });
    else
        NativeVisibilityTest(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeCriHandleSearch: 83167518, a search of the CRI sound library's list of buffer handles (CRI calls it
// through a pointer, the game thread among others). For each node it makes two checks through two helpers, and both
// call a third for the padding that aligns the end of the node's 28-byte header: an integer division, twice a node. Here the helpers are folded into the loop and the padding comes from a mask when the
// alignment is a power of two (the same value as the division's remainder: the header ends below 4 GB); any other
// case divides as the guest does. The back-chain words of the frames are written, and r4-r10 are rebuilt at the end
// as the last helper call left them (its quotient divided out only then).

// The padding helper (r3, r4) with r9 coming in as r9In: what it leaves in r3, r8, r9 and r10.
static void CriPadding(uint64_t r3In, uint64_t r4In, uint64_t r9In, uint64_t& r3, uint64_t& r8, uint64_t& r9, uint64_t& r10)
{
    uint64_t r11 = r4In;
    r10 = r3In + 28;
    if (int32_t(uint32_t(r11)) < 8)
        r11 = 8;
    r9 = (r9In & 0xFFFFFFFF00000000ull) | (uint32_t(r10) / uint32_t(r11));
    r8 = uint64_t(int64_t(int32_t(uint32_t(r9))) * int64_t(int32_t(uint32_t(r11))));
    r3 = r10 - r8;
    if (int32_t(uint32_t(r3)) != 0)
        r3 = r11 - r3;
}

// Just the padding (r3) of that helper, for an alignment argument of at most 0xFFFF.
static inline uint64_t CriPaddingOnly(uint64_t node, uint64_t align16)
{
    const uint64_t align = int32_t(uint32_t(align16)) < 8 ? 8 : align16;
    const uint64_t end = node + 28;
    const uint32_t a = uint32_t(align);
    if ((end >> 32) == 0 && (a & (a - 1)) == 0)
    {
        const uint64_t remainder = end & (a - 1);
        return remainder != 0 ? align - remainder : 0;
    }

    uint64_t r3, r8, r9, r10;
    CriPadding(node, align16, 0, r3, r8, r9, r10);
    return r3;
}

static void NativeCriHandleSearch(PPCContext& ctx, uint8_t* base)
{
    const uint32_t sp = ctx.r1.u32;
    PPC_STORE_U32(sp - 112, sp);

    const uint64_t r30 = ctx.r4.u64;
    const uint64_t r29 = ctx.r5.u64;
    const uint64_t align16 = uint32_t(r29) & 0xFFFF;
    uint64_t r31 = PPC_LOAD_U32_D(ctx.r3.u32, 20);

    // The last helper call: 1 = the first check, 2 = the second; its node and padding, and the first check's values.
    uint32_t last = 0;
    uint64_t lastNode = 0, lastPadding = 0;
    uint64_t firstR4 = 0, firstR5 = 0, firstR8 = 0, firstR10 = 0;
    uint64_t secondR6 = 0;
    bool found = false;

    while (true)
    {
        if (PPC_LOAD_U8_D(uint32_t(r31), 12) == 0)
        {
            // The first check (r31, r30, r29): r3 = 1 unless the padded header end passes the buffer.
            const uint64_t padding = CriPaddingOnly(r31, align16);
            const uint64_t r8 = PPC_LOAD_U16_D(uint32_t(r31), 16);
            const uint64_t r11 = PPC_LOAD_U16_D(uint32_t(r31), 14);
            const uint64_t r9 = uint32_t(padding) & 0xFFFF;
            const uint64_t r10 = PPC_LOAD_U32_D(uint32_t(r31), 8);
            const uint64_t r5 = r9 + r30;
            const uint64_t r4 = (r8 + r11) + r10;
            last = 1;
            lastNode = r31;
            lastPadding = padding;
            firstR4 = r4;
            firstR5 = r5;
            firstR8 = r8;
            firstR10 = r10;

            if (!(int32_t(uint32_t(r5)) > int32_t(uint32_t(r4))))
            {
                const uint32_t state = PPC_LOAD_U8_D(uint32_t(r31), 13);
                if (state == 0 || state == 2)
                {
                    found = true;
                    break;
                }
            }

            // Not a free one: is it the owner's?
            const uint64_t owner = PPC_LOAD_U32_D(uint32_t(r31), 8);
            if (int32_t(uint32_t(owner)) == int32_t(uint32_t(r30)))
            {
                // The second check (r31, r29): r3 = 1 when the padding equals the halfword at 14. Its padding call
                // gets the same node and alignment as the first check's, so the same padding.
                const uint64_t r6 = PPC_LOAD_U16_D(uint32_t(r31), 14);
                last = 2;
                secondR6 = r6;
                if (uint32_t((uint32_t(padding) & 0xFFFF) - r6) == 0)
                {
                    if (PPC_LOAD_U8_D(uint32_t(r31), 13) == 2)
                    {
                        found = true;
                        break;
                    }
                }
            }
        }

        // The next node.
        r31 = PPC_LOAD_U32_D(uint32_t(r31), 0);
        if (uint32_t(r31) == 0)
            break;
    }

    if (last != 0)
    {
        // The helpers' frame (stwu r1,-96(r1) below this function's).
        PPC_STORE_U32(sp - 112 - 96, sp - 112);
    }

    if (last == 1)
    {
        ctx.r4.u64 = firstR4;
        ctx.r5.u64 = firstR5;
        ctx.r6.u64 = lastNode;
        ctx.r7.u64 = r30;
        ctx.r8.u64 = firstR8;
        ctx.r9.u64 = uint32_t(lastPadding) & 0xFFFF;
        ctx.r10.u64 = firstR10;
    }
    else if (last == 2)
    {
        // The second check after the first in the same pass, which left r9 = padding & 0xFFFF.
        uint64_t r3, r8, r9, r10;
        CriPadding(lastNode, align16, uint32_t(lastPadding) & 0xFFFF, r3, r8, r9, r10);
        const uint64_t r5 = uint32_t(r3) & 0xFFFF;
        ctx.r4.u64 = r5 - secondR6;
        ctx.r5.u64 = r5;
        ctx.r6.u64 = secondR6;
        ctx.r7.u64 = lastNode;
        ctx.r8.u64 = r8;
        ctx.r9.u64 = r9;
        ctx.r10.u64 = r10;
    }

    ctx.r3.u64 = found ? r31 : 0;
}

static bool g_nativeCriHandleSearch = false;

PPC_FUNC_IMPL(__imp__sub_83167518);
PPC_FUNC(sub_83167518)
{
    if (!g_nativeCriHandleSearch)
        __imp__sub_83167518(ctx, base);
    else if (g_verify && VerifyThisCall())
        Verify("CRI handle search", ctx, base, __imp__sub_83167518, NativeCriHandleSearch, { { ctx.r1.u32 - 208, 100 } });
    else
        NativeCriHandleSearch(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// SwitchNativeNameCompare: 82DFAF58, the "less than" of the engine's shared strings (241 call sites, the name maps
// that resources and effects are looked up in). It gets each string's characters through a leaf helper (the pointer
// it holds, or a static empty string when that is null; the guest builds that address sign-extended) and compares
// bytes up to the first difference or the end of the left one. Both helper calls folded in; the same loads, the
// back-chain word, and r9 and r10 as the loop leaves them.

static void NativeNameCompare(PPCContext& ctx, uint8_t* base)
{
    const uint32_t sp = ctx.r1.u32;
    PPC_STORE_U32(sp - 96, sp);

    auto characters = [base](uint64_t holder) -> uint64_t
        {
            const uint64_t pointer = PPC_LOAD_U32_D(uint32_t(holder), 0);
            return uint32_t(pointer) != 0 ? pointer : uint64_t(int64_t(-2112749568) + -15668);
        };

    const uint64_t left = ctx.r3.u64;
    uint64_t r10 = characters(ctx.r4.u64);
    uint64_t r3 = characters(left);
    uint64_t r9;
    int64_t difference;
    while (true)
    {
        const uint64_t r11 = PPC_LOAD_U8_D(uint32_t(r3), 0);
        r9 = PPC_LOAD_U8_D(uint32_t(r10), 0);
        difference = int64_t(r11) - int64_t(r9);
        if (uint32_t(r11) == 0)
            break;
        r3 += 1;
        r10 += 1;
        if (int32_t(difference) != 0)
            break;
    }

    ctx.r9.u64 = r9;
    ctx.r10.u64 = r10;
    ctx.r3.u64 = (uint32_t(difference) >> 31) & 1;
}

static bool g_nativeNameCompare = false;

PPC_FUNC_IMPL(__imp__sub_82DFAF58);
PPC_FUNC(sub_82DFAF58)
{
    if (!g_nativeNameCompare)
        __imp__sub_82DFAF58(ctx, base);
    else if (g_verify && VerifyThisCall())
        Verify("name compare", ctx, base, __imp__sub_82DFAF58, NativeNameCompare, { { ctx.r1.u32 - 96, 4 } });
    else
        NativeNameCompare(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------
// Round 13, SwitchNativeLightField: 82E2B780, the sample of one cell of a stage's light field (the ambient light each
// object gets; up to 20% of the game thread in light-field-heavy stages). It decodes the cell's 8 corner records and
// blends them in three steps, through two helpers:
// - the decoder (at -0x648): 25 bytes to 25 floats: (byte / 256)^2 for the first 24 (vperm with the tables at
//   0x8211D080 puts each byte in a word of its own; vcsxwfp, then a square: every step exact) and byte 24 times 1/255
//   in single precision, at 4-byte steps from the record's start (the stvewx stores);
// - the blend (at -0xC0): seven calls of the lerp (at -0x2D8), out = a + (b - a) * t for the 24 vector lanes (vsubfp,
//   vmaddfp; GCC fused the multiply-add into fmla, run with flush to zero) and (1 - t) * a + b * t for the last
//   float (fmuls, fsubs, fmadds: products and differences rounded to single precision, the multiply-add fused, run
//   without flush to zero).
// Each helper call switched the flush-to-zero mode twice (msr fpcr, on the lines with a quarter of these functions'
// samples) and stored its results a word at a time. Here the decodes and the first six blends run natively, with the
// vector lanes under one flush-to-zero section, exactly as computed above; the two buffers the last blend reads are
// written to the stack where the blend helper keeps them, and the last blend is the recompiled lerp itself, called as
// the blend helper calls it, so that every register is left exactly as the recompiled code leaves it.
namespace
{
    constexpr uint32_t LIGHT_FIELD_CELL = 0x82E2B780;
    constexpr uint32_t LIGHT_FIELD_LERP = LIGHT_FIELD_CELL - 0x2D8;
    bool g_nativeLightField = false;
    double g_lightFieldByteScale;   // 1/255 in single precision, read from the game's constants
    double g_lightFieldOne;         // 1.0, likewise

    struct LightFieldRecord
    {
        alignas(16) float v[28];    // 24 vector lanes, the last float at 24
    };

    // The mode switch (msr fpcr) is a volatile asm without a memory clobber: GCC keeps volatile asms in order, but may
    // move other loads and arithmetic across them. Each value is passed through an empty volatile asm after the switch
    // it must follow (and each result through one before the switch it must precede), which pins every operation
    // between the two switches it belongs between.
    template<typename T>
    T Pinned(T value)
    {
        __asm__ __volatile__("" : "+w"(value));
        return value;
    }

    double LoadSingle(uint8_t* base, uint32_t address)
    {
        return Pinned(double(std::bit_cast<float>(PPC_LOAD_U32(address))));
    }

    double RoundSingle(double value)
    {
        return double(float(value));
    }

    void DecodeLightFieldRecord(uint8_t* base, uint32_t address, LightFieldRecord& out)
    {
        for (uint32_t i = 0; i < 24; i++)
        {
            const float value = float(int32_t(PPC_LOAD_U8(address + i))) * 0.00390625f;
            out.v[i] = value * value;
        }

        const double last = double(float(double(int64_t(PPC_LOAD_U8(address + 24)))));
        out.v[24] = float(last * g_lightFieldByteScale);
    }

    // Under flush to zero, like the recompiled vsubfp/vmaddfp. The weight is converted before the section (the
    // recompiled code stores it with stfs before switching); a denormal weight is flushed by fmla either way.
    void LerpLightFieldLanes(const LightFieldRecord& a, const LightFieldRecord& b, float32x4_t weight, LightFieldRecord& out)
    {
        weight = Pinned(weight);
        for (uint32_t i = 0; i < 24; i += 4)
        {
            const float32x4_t from = Pinned(vld1q_f32(a.v + i));
            const float32x4_t to = Pinned(vld1q_f32(b.v + i));
            vst1q_f32(out.v + i, Pinned(vfmaq_f32(from, vsubq_f32(to, from), weight)));
        }
    }

    // Without flush to zero, like the recompiled fmuls, fsubs and fmadds.
    void LerpLightFieldLast(const LightFieldRecord& a, const LightFieldRecord& b, double t, LightFieldRecord& out)
    {
        t = Pinned(t);
        const double bt = RoundSingle(Pinned(double(b.v[24])) * t);
        const double rest = RoundSingle(Pinned(g_lightFieldOne) - t);
        out.v[24] = Pinned(float(std::fma(rest, Pinned(double(a.v[24])), bt)));
    }

    void StoreLightFieldRecord(uint8_t* base, uint32_t address, const LightFieldRecord& record)
    {
        for (uint32_t i = 0; i < 25; i++)
            PPC_STORE_U32(address + i * 4, std::bit_cast<uint32_t>(record.v[i]));
    }
}

static void NativeLightFieldCell(PPCContext& ctx, uint8_t* base)
{
    const uint32_t object = ctx.r3.u32;
    const uint32_t cell = ctx.r5.u32;
    const uint32_t box = ctx.r6.u32;
    const uint32_t position = ctx.r7.u32;
    const uint64_t stack = ctx.r1.u64;

    // The cell's own arithmetic, in the recompiled order (lfs, then fsubs and fdivs rounded to single precision).
    ctx.fpscr.disableFlushMode();
    double f0 = LoadSingle(base, box + 12);
    double f11 = LoadSingle(base, position + 0);
    double f10 = LoadSingle(base, box + 16);
    f11 = RoundSingle(f11 - f0);
    double f13 = LoadSingle(base, box + 20);
    f0 = RoundSingle(f10 - f0);
    double f9 = LoadSingle(base, position + 4);
    f10 = LoadSingle(base, box + 24);
    f9 = RoundSingle(f9 - f13);
    double f12 = LoadSingle(base, box + 28);
    f13 = RoundSingle(f10 - f13);
    double f8 = LoadSingle(base, position + 8);
    f10 = LoadSingle(base, box + 32);
    f8 = Pinned(RoundSingle(f8 - f12));
    f12 = RoundSingle(f10 - f12);
    f9 = Pinned(f9);
    f10 = Pinned(f10);
    const double w3 = Pinned(RoundSingle(f11 / f0));
    const double w2 = Pinned(RoundSingle(f9 / f13));
    const double w1 = Pinned(RoundSingle(f8 / f12));
    const float32x4_t lanes1 = Pinned(vdupq_n_f32(float(w1)));
    const float32x4_t lanes2 = Pinned(vdupq_n_f32(float(w2)));

    // The 8 corner records.
    const uint32_t header = PPC_LOAD_U32(object);
    const uint32_t records = PPC_LOAD_U32(header + 64);
    const uint32_t indices = PPC_LOAD_U32(header + 76);
    const uint32_t first = PPC_LOAD_U32(cell + 4);
    LightFieldRecord corner[8];
    for (uint32_t i = 0; i < 8; i++)
    {
        const uint32_t index = PPC_LOAD_U32(indices + ((first + i) << 2));
        DecodeLightFieldRecord(base, records + index * 25, corner[i]);
    }

    // The blend helper's first six lerps.
    LightFieldRecord l1, l2, l3, l4, l5, l6;
    ctx.fpscr.enableFlushMode();
    LerpLightFieldLanes(corner[0], corner[1], lanes1, l1);
    LerpLightFieldLanes(corner[2], corner[3], lanes1, l2);
    LerpLightFieldLanes(l1, l2, lanes2, l3);
    LerpLightFieldLanes(corner[4], corner[5], lanes1, l4);
    LerpLightFieldLanes(corner[6], corner[7], lanes1, l5);
    LerpLightFieldLanes(l4, l5, lanes2, l6);
    ctx.fpscr.disableFlushMode();
    LerpLightFieldLast(corner[0], corner[1], w1, l1);
    LerpLightFieldLast(corner[2], corner[3], w1, l2);
    LerpLightFieldLast(l1, l2, w2, l3);
    LerpLightFieldLast(corner[4], corner[5], w1, l4);
    LerpLightFieldLast(corner[6], corner[7], w1, l5);
    LerpLightFieldLast(l4, l5, w2, l6);

    // The last lerp, as the blend helper calls it: its frame is 576 bytes below the cell's, 992 below the caller's.
    const uint64_t blendStack = (stack & ~0xFFFFFFFFull) | uint32_t(uint32_t(stack) - 992 - 576);
    StoreLightFieldRecord(base, uint32_t(blendStack) + 416, l3);
    StoreLightFieldRecord(base, uint32_t(blendStack) + 304, l6);

    ctx.r1.u64 = blendStack;
    ctx.r3.u64 = object;
    ctx.r5.s64 = int64_t(blendStack) + 416;
    ctx.r6.s64 = int64_t(blendStack) + 304;
    ctx.f1.f64 = w3;
    ctx.f2.f64 = w2;
    ctx.f3.f64 = w3;
    ctx.f8.f64 = f8;
    ctx.f9.f64 = f9;
    ctx.f10.f64 = f10;
    ctx.f11.f64 = w1;
    (PPC_LOOKUP_FUNC(base, LIGHT_FIELD_LERP))(ctx, base);
    ctx.r1.u64 = stack;
}

PPC_FUNC_IMPL(__imp__sub_82E2B780);
PPC_FUNC(sub_82E2B780)
{
    if (!g_nativeLightField)
    {
        __imp__sub_82E2B780(ctx, base);
    }
    else if (g_verify && VerifyThisCall())
    {
        const uint32_t blendStack = ctx.r1.u32 - 992 - 576;
        Verify("light field cell", ctx, base, __imp__sub_82E2B780, NativeLightFieldCell,
            { { ctx.r4.u32, 100 }, { blendStack + 304, 100 }, { blendStack + 416, 100 } });
    }
    else
    {
        NativeLightFieldCell(ctx, base);
    }
}

// ---------------------------------------------------------------------------------------------------------------

void InitNativeHotFunctions(uint8_t* base)
{
    const bool mapFind = Config::SwitchNativeMapFind;
    const bool quatDecode = Config::SwitchNativeQuatDecode;
    const bool bonePalette = Config::SwitchNativeBonePalette;
    const bool layerMaskTest = Config::SwitchNativeLayerMaskTest;
    const bool visibilityTest = Config::SwitchNativeVisibilityTest;
    const bool criHandleSearch = Config::SwitchNativeCriHandleSearch;
    const bool nameCompare = Config::SwitchNativeNameCompare;
    const bool lightField = Config::SwitchNativeLightField;
    g_fastResourceWaits = Config::SwitchFastResourceWaits;
    if (!mapFind && !quatDecode && !bonePalette && !layerMaskTest && !visibilityTest && !criHandleSearch && !nameCompare &&
        !lightField)
    {
        if (g_fastResourceWaits)
            fprintf(stderr, "[native] resource waits poll every 0.5 ms\n");
        return;
    }

    auto matches = [&](uint32_t address, uint32_t size, uint64_t hash)
        {
            const bool same = HashCode(base, address, size) == hash;
            if (!same)
                fprintf(stderr, "[native] the guest code at %08X is not the expected one; the recompiled code runs.\n", address);
            return same;
        };

    uint32_t mapFinds = 0;
    for (MapFindHook& hook : g_mapFinds)
    {
        hook.active = mapFind && matches(hook.address, 0x80, hook.codeHash);
        mapFinds += hook.active;
    }

    g_nativeQuatDecode = quatDecode && matches(0x82FCFA08, 0x108, 0x19B45C04116D56FDull);
    g_nativeBonePalette = bonePalette && matches(0x82E18118, 0x308, 0x3FA89C2D56C4938Bull);
    g_nativeLayerMaskTest = layerMaskTest && matches(0x82E26688, 0xA8, 0x5D2CE17479D4FAEDull);

    // Round 12: each was written from the hooked function and the helpers it folds in, all checked. The helpers'
    // addresses are given relative to the hooked one: tools/switch-direct-calls.py takes any guest function address
    // written in these sources for a hooked one, and their callers would then lose their direct calls to them.
    constexpr uint32_t visibilityTestAddress = 0x82E26498;
    constexpr uint32_t criHandleSearchAddress = 0x83167518;
    constexpr uint32_t nameCompareAddress = 0x82DFAF58;
    g_nativeVisibilityTest = visibilityTest && matches(visibilityTestAddress, 0x10C, 0xDFC108DD5AACE376ull) &&
        matches(visibilityTestAddress - 0x2500, 0x40, 0xD86AFE143A981578ull);
    g_renderWalkPrefetch = g_nativeVisibilityTest && Config::SwitchRenderWalkPrefetch;
    g_nativeCriHandleSearch = criHandleSearch && matches(criHandleSearchAddress, 0xA0, 0xFEEEE7BAD5D38F1Dull) &&
        matches(criHandleSearchAddress - 0xD0, 0x58, 0x0E6BF51951479706ull) &&
        matches(criHandleSearchAddress - 0x110, 0x3C, 0x25373BB0215B07E2ull) &&
        matches(criHandleSearchAddress - 0x1E0, 0x2C, 0xFCECEC0A3A322B4Cull);
    g_nativeNameCompare = nameCompare && matches(nameCompareAddress, 0x64, 0x09DCB76085814B27ull) &&
        matches(nameCompareAddress - 0x88, 0x54, 0x7A2D98143F9FE7C4ull);

    // Round 13: the cell, its blend helper, the lerp and the decoder, then the decoder's permute tables and byte mask,
    // 1/255 and 1.0 (data below the code, so no function address).
    g_nativeLightField = lightField && matches(LIGHT_FIELD_CELL, 0xDC, 0xB6A9D9FD4469A32Full) &&
        matches(LIGHT_FIELD_CELL - 0xC0, 0xC0, 0x4F50CD44A05B9A84ull) &&
        matches(LIGHT_FIELD_LERP, 0x218, 0x4E06D3CDFCED607Eull) &&
        matches(LIGHT_FIELD_CELL - 0x648, 0x1CC, 0xC80CDD6039C40A7Full) &&
        matches(0x8211D080, 0x40, 0xFD755061692E04A5ull) && matches(0x820041A0, 0x10, 0x780D5836696931DDull) &&
        matches(0x82009EB8, 4, 0x9A40C342CFC90631ull) && matches(0x820008C4, 4, 0xC21D92265D7E4F3Aull);
    if (g_nativeLightField)
    {
        g_lightFieldByteScale = double(std::bit_cast<float>(PPC_LOAD_U32(0x82009EB8)));
        g_lightFieldOne = double(std::bit_cast<float>(PPC_LOAD_U32(0x820008C4)));
    }

    g_verify = Config::SwitchVerifyNativeHotFunctions;

    fprintf(stderr, "[native] map find %u of 6, quaternion decoder %s, bone palette %s, layer mask test %s, visibility test %s%s, "
        "CRI handle search %s, name compare %s, light field %s; resource waits poll every %s%s\n", mapFinds,
        g_nativeQuatDecode ? "on" : "off", g_nativeBonePalette ? "on" : "off", g_nativeLayerMaskTest ? "on" : "off",
        g_nativeVisibilityTest ? "on" : "off", g_renderWalkPrefetch ? " with prefetch" : "",
        g_nativeCriHandleSearch ? "on" : "off", g_nativeNameCompare ? "on" : "off", g_nativeLightField ? "on" : "off",
        g_fastResourceWaits ? "0.5 ms" : "5 ms", g_verify ? " (verifying against the recompiled code)" : "");
}

#endif
