// [Switch] Round 15: main-thread clusters as copies of their recompiled code with the registers in locals
// (SwitchNativeSplineAnimation, SwitchNativePathFollowing, SwitchNativeMoppVm).
#include <stdafx.h>

#if defined(__SWITCH__)

#include <bit>
#include <csetjmp>
#include <user/config.h>
#include "native_r15.h"
#include "native_verify.h"
#include "verify_sampling.h"

// The round 14 stage profile's hottest recompiled clusters on the game thread, after the ones native code already
// covers: the Havok spline-compressed animation sampler (82FC4390 and the six helpers it calls per track: 3.5 % of
// the hub's game thread), the path-following projection (822D22C8 and its twelve helpers: 3.7 % in stages, a third of
// it in flush-mode switches), and Havok's two MOPP virtual machines (1.9 % in stages).
//
// Each is the recompiled code itself (tools/switch-localize.py, UnleashedRecompLib/switch/localized_*.inl): the same
// statements in the same order, so the same guest loads and stores and the same floating-point expressions, with the
// context's registers in a LocalRegs object on the hook's stack, which GCC keeps in registers. For the spline and
// path clusters the helpers are copied into the caller, so the whole cluster runs without their calls, prologues and
// context traffic. The FPSCR's cached flush mode is a local too, so GCC sees which mode is set and drops a switch to
// the mode already set (every switch where the mode changes is still made; the hardware register always holds the
// cached mode). Calls out of the copied code (virtual calls, the VMs' recursion and hit collectors) get the context
// written back before and read again after, exactly as the recompiled function leaves and finds it.
//
// Exactness of the floating-point statements needs explicit fused multiply-adds and -ffp-contract=off
// (SWITCH_EXPLICIT_FMA): then each statement rounds as written, in the copy as in the original, whatever GCC does
// around it. Without that build option these copies are not compiled and the keys do nothing.
//
// SwitchVerifyNativeHotFunctions runs the copy with its guest stores logged, undoes them, runs the recompiled function
// and compares the stores' final values and the registers (a call out of the copied code stops the copy there; that
// call is then not compared).

#if defined(UNLEASHED_RECOMP_SWITCH_EXPLICIT_FMA)

namespace localized_main
{
#define LOCAL_BEFORE_CALL() ((void)0)
#include <switch/localized_main.inl>
#undef LOCAL_BEFORE_CALL
}

namespace localized_mopp_query
{
#define LOCAL_BEFORE_CALL() ((void)0)
#include <switch/localized_mopp_query.inl>
#undef LOCAL_BEFORE_CALL
}

namespace localized_mopp_ray
{
#define LOCAL_BEFORE_CALL() ((void)0)
#include <switch/localized_mopp_ray.inl>
#undef LOCAL_BEFORE_CALL
}

namespace
{
    struct StoreRecord
    {
        size_t address;
        uint32_t size;
        uint64_t previous;
    };

    thread_local std::vector<StoreRecord>* t_storeLog = nullptr;
    thread_local std::jmp_buf* t_leaveCopy = nullptr;

    template<typename T>
    void LoggedStore(uint8_t* base, size_t address, T value)
    {
        StoreRecord record{ address, sizeof(T), 0 };
        memcpy(&record.previous, base + address, sizeof(T));
        t_storeLog->push_back(record);
        memcpy(base + address, &value, sizeof(T));
    }

    [[noreturn]] void LeaveCopy()
    {
        std::longjmp(*t_leaveCopy, 1);
    }
}

#pragma push_macro("PPC_STORE_U8")
#pragma push_macro("PPC_STORE_U16")
#pragma push_macro("PPC_STORE_U32")
#pragma push_macro("PPC_STORE_U64")
#pragma push_macro("PPC_STORE_U8_D")
#pragma push_macro("PPC_STORE_U16_D")
#pragma push_macro("PPC_STORE_U32_D")
#pragma push_macro("PPC_STORE_U64_D")
#undef PPC_STORE_U8
#undef PPC_STORE_U16
#undef PPC_STORE_U32
#undef PPC_STORE_U64
#undef PPC_STORE_U8_D
#undef PPC_STORE_U16_D
#undef PPC_STORE_U32_D
#undef PPC_STORE_U64_D
#define PPC_STORE_U8(x, y) LoggedStore<uint8_t>(base, (x), uint8_t(y))
#define PPC_STORE_U16(x, y) LoggedStore<uint16_t>(base, (x), __builtin_bswap16(y))
#define PPC_STORE_U32(x, y) LoggedStore<uint32_t>(base, (x), __builtin_bswap32(y))
#define PPC_STORE_U64(x, y) LoggedStore<uint64_t>(base, (x), __builtin_bswap64(y))
#define PPC_STORE_U8_D(r, d, y) LoggedStore<uint8_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), uint8_t(y))
#define PPC_STORE_U16_D(r, d, y) LoggedStore<uint16_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap16(y))
#define PPC_STORE_U32_D(r, d, y) LoggedStore<uint32_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap32(y))
#define PPC_STORE_U64_D(r, d, y) LoggedStore<uint64_t>(base, size_t(uint64_t(uint32_t(r)) + int64_t(d)), __builtin_bswap64(y))
#define LOCAL_BEFORE_CALL() LeaveCopy()

namespace logged_main
{
#include <switch/localized_main.inl>
}

namespace logged_mopp_query
{
#include <switch/localized_mopp_query.inl>
}

namespace logged_mopp_ray
{
#include <switch/localized_mopp_ray.inl>
}

#undef LOCAL_BEFORE_CALL
#pragma pop_macro("PPC_STORE_U8")
#pragma pop_macro("PPC_STORE_U16")
#pragma pop_macro("PPC_STORE_U32")
#pragma pop_macro("PPC_STORE_U64")
#pragma pop_macro("PPC_STORE_U8_D")
#pragma pop_macro("PPC_STORE_U16_D")
#pragma pop_macro("PPC_STORE_U32_D")
#pragma pop_macro("PPC_STORE_U64_D")

namespace
{
    bool g_localizedSpline = false;
    bool g_localizedPath = false;
    bool g_localizedMopp = false;

    std::atomic<uint64_t> g_leftCopy;

    template<typename F>
    struct RegsOf;

    template<typename R>
    struct RegsOf<void (*)(R&, PPCContext&, uint8_t*)>
    {
        using type = R;
    };

    uint64_t ReadFpcr()
    {
        uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        return fpcr;
    }

    void WriteFpcr(uint64_t fpcr)
    {
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
    }

    // Not inlined into the hook: the MOPP machines recurse through their hooks, and each level's frame should hold only
    // the copy that runs (the logged copy's locals live here, while verifying).
    template<auto Logged>
    __attribute__((noinline)) void VerifyLocalized(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        using LoggedRegs = typename RegsOf<decltype(Logged)>::type;

        std::vector<StoreRecord> log;
        std::jmp_buf leave;
        PPCContext copy = ctx;
        const uint64_t fpcr = ReadFpcr();
        bool left = false;

        t_storeLog = &log;
        t_leaveCopy = &leave;
        if (setjmp(leave) == 0)
        {
            LoggedRegs regs;
            regs.Load(copy);
            Logged(regs, copy, base);
            regs.Store(copy);
        }
        else
        {
            left = true;
        }
        t_storeLog = nullptr;
        t_leaveCopy = nullptr;

        // The copy's results, then guest memory as it was, the FPCR included (the recompiled code trusts its FPSCR).
        std::vector<uint64_t> results(log.size());
        for (size_t i = 0; i < log.size(); i++)
            memcpy(&results[i], base + log[i].address, log[i].size);
        for (size_t i = log.size(); i-- > 0;)
            memcpy(base + log[i].address, &log[i].previous, log[i].size);
        WriteFpcr(fpcr);

        original(ctx, base);

        if (left)
        {
            const uint64_t count = g_leftCopy.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((count & (count - 1)) == 0 && count >= 1024)
                fprintf(stderr, "[native] localized copies: %llu verified calls left the copy (not compared)\n", (unsigned long long)count);
            return;
        }

        bool same = true;
        for (size_t i = 0; i < log.size(); i++)
        {
            uint64_t value = 0, result = results[i];
            memcpy(&value, base + log[i].address, log[i].size);
            if (log[i].size < 8)
                result &= (uint64_t(1) << (log[i].size * 8)) - 1;
            same &= value == result;
        }

        // Every register the context holds (r3, r1, r4-r10, r13, the FPSCR, f1-f13, v0-v13).
        const size_t registers = reinterpret_cast<const uint8_t*>(&ctx.v13) + sizeof(PPCVRegister) -
            reinterpret_cast<const uint8_t*>(&ctx.r3);
        same &= memcmp(&ctx.r3, &copy.r3, registers) == 0;

        native_verify::ReportVerify(name, same);
    }

    template<auto Localized, auto Logged>
    void RunLocalized(const char* name, bool enabled, PPCContext& ctx, uint8_t* base, PPCFunc* original)
    {
        if (!enabled)
        {
            original(ctx, base);
            return;
        }

        if (native_verify::g_verify && VerifyThisCall())
        {
            VerifyLocalized<Logged>(name, ctx, base, original);
            return;
        }

        typename RegsOf<decltype(Localized)>::type regs;
        regs.Load(ctx);
        Localized(regs, ctx, base);
        regs.Store(ctx);
    }
}

// SwitchNativeSplineAnimation: the spline sampler, its six helpers copied in.
PPC_FUNC_IMPL(__imp__sub_82FC4390);
PPC_FUNC(sub_82FC4390)
{
    RunLocalized<localized_main::Local_82FC4390, logged_main::Local_82FC4390>("spline sampler", g_localizedSpline, ctx, base,
        __imp__sub_82FC4390);
}

// SwitchNativePathFollowing: the path projection, its twelve helpers copied in.
PPC_FUNC_IMPL(__imp__sub_822D22C8);
PPC_FUNC(sub_822D22C8)
{
    RunLocalized<localized_main::Local_822D22C8, logged_main::Local_822D22C8>("path following", g_localizedPath, ctx, base,
        __imp__sub_822D22C8);
}

// SwitchNativeMoppVm: the two virtual machines (each recursion comes back through its hook).
PPC_FUNC_IMPL(__imp__sub_82F78148);
PPC_FUNC(sub_82F78148)
{
    RunLocalized<localized_mopp_query::Local_82F78148, logged_mopp_query::Local_82F78148>("MOPP query", g_localizedMopp, ctx,
        base, __imp__sub_82F78148);
}

PPC_FUNC_IMPL(__imp__sub_82F78FB0);
PPC_FUNC(sub_82F78FB0)
{
    RunLocalized<localized_mopp_ray::Local_82F78FB0, logged_mopp_ray::Local_82F78FB0>("MOPP long ray", g_localizedMopp, ctx,
        base, __imp__sub_82F78FB0);
}

void InitNativeSplineAnimation(uint8_t*)
{
    g_localizedSpline = Config::SwitchNativeSplineAnimation;
    if (g_localizedSpline)
        fprintf(stderr, "[native] spline sampler with its helpers, registers in locals%s\n", native_verify::g_verify ? " (verifying)" : "");
}

void InitNativePathFollowing(uint8_t*)
{
    g_localizedPath = Config::SwitchNativePathFollowing;
    if (g_localizedPath)
        fprintf(stderr, "[native] path following with its helpers, registers in locals%s\n", native_verify::g_verify ? " (verifying)" : "");
}

void InitNativeMoppVm(uint8_t*)
{
    g_localizedMopp = Config::SwitchNativeMoppVm;
    if (g_localizedMopp)
        fprintf(stderr, "[native] MOPP virtual machines, registers in locals%s\n", native_verify::g_verify ? " (verifying)" : "");
}

#else

void InitNativeSplineAnimation(uint8_t*)
{
    if (Config::SwitchNativeSplineAnimation)
        fprintf(stderr, "[native] SwitchNativeSplineAnimation needs a build with SWITCH_EXPLICIT_FMA=1; off\n");
}

void InitNativePathFollowing(uint8_t*)
{
    if (Config::SwitchNativePathFollowing)
        fprintf(stderr, "[native] SwitchNativePathFollowing needs a build with SWITCH_EXPLICIT_FMA=1; off\n");
}

void InitNativeMoppVm(uint8_t*)
{
    if (Config::SwitchNativeMoppVm)
        fprintf(stderr, "[native] SwitchNativeMoppVm needs a build with SWITCH_EXPLICIT_FMA=1; off\n");
}

#endif

#endif
