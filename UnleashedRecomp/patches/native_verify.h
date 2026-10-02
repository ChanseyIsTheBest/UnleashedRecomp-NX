#pragma once

// [Switch] Round 15: the helpers every native replacement of a guest function uses (native_hot_patches.cpp since
// round 11, and the round 15 natives): the check that the guest code is the code a native was written from, and the
// verify mode (SwitchVerifyNativeHotFunctions) that runs the recompiled function and the native one on the same input
// and compares registers and memory, keeping the recompiled function's effects.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "verify_sampling.h"

namespace native_verify
{
    // SwitchVerifyNativeHotFunctions; set by InitNativeHotFunctions before guest code runs (and before the round 15
    // natives are set up).
    extern bool g_verify;

    // FNV-1a over the guest code of a function.
    inline uint64_t HashCode(const uint8_t* base, uint32_t address, uint32_t size)
    {
        uint64_t hash = 0xCBF29CE484222325ull;
        for (uint32_t i = 0; i < size; i++)
        {
            hash ^= base[address + i];
            hash *= 0x100000001B3ull;
        }

        return hash;
    }

    // Whether the guest code at address is the expected one; says so on stderr when it is not (the recompiled code
    // then runs).
    inline bool CodeMatches(const uint8_t* base, uint32_t address, uint32_t size, uint64_t hash)
    {
        const bool same = HashCode(base, address, size) == hash;
        if (!same)
            fprintf(stderr, "[native] the guest code at %08X is not the expected one; the recompiled code runs.\n", address);
        return same;
    }

    // Counts a verified call; prints "[native] MISMATCH n in name" for the first 32 mismatches and every 1024th.
    void ReportVerify(const char* name, bool same);

    struct Region
    {
        uint32_t address;
        uint32_t size;
    };

    // Verification: run the recompiled function, then the native one on a copy of the context with the guest memory
    // both write put back as it was; compare both, then leave the recompiled function's results. Regions may overlap.
    template<typename Native>
    void VerifyRegions(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original, Native native,
        const Region* regions, size_t count)
    {
        std::vector<uint8_t> before, recompiled, mine;
        for (size_t i = 0; i < count; i++)
            before.insert(before.end(), base + regions[i].address, base + regions[i].address + regions[i].size);

        PPCContext copy = ctx;
        original(ctx, base);

        for (size_t i = 0; i < count; i++)
            recompiled.insert(recompiled.end(), base + regions[i].address, base + regions[i].address + regions[i].size);

        size_t offset = 0;
        for (size_t i = 0; i < count; i++)
        {
            memcpy(base + regions[i].address, before.data() + offset, regions[i].size);
            offset += regions[i].size;
        }

        native(copy, base);

        for (size_t i = 0; i < count; i++)
            mine.insert(mine.end(), base + regions[i].address, base + regions[i].address + regions[i].size);

        // Every register the context holds with this configuration: r3, r1, r4-r10, r13, the FPSCR, f1-f13, v0-v13.
        const size_t registers = reinterpret_cast<const uint8_t*>(&ctx.v13) + sizeof(PPCVRegister) -
            reinterpret_cast<const uint8_t*>(&ctx.r3);
        const bool same = recompiled == mine && memcmp(&ctx.r3, &copy.r3, registers) == 0;

        offset = 0;
        for (size_t i = 0; i < count; i++)
        {
            memcpy(base + regions[i].address, recompiled.data() + offset, regions[i].size);
            offset += regions[i].size;
        }

        ReportVerify(name, same);
    }

    template<typename Native>
    void Verify(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original, Native native,
        std::initializer_list<Region> regions)
    {
        VerifyRegions(name, ctx, base, original, native, regions.begin(), regions.size());
    }

    // For natives whose written memory depends on their input (a count of buffers).
    template<typename Native>
    void Verify(const char* name, PPCContext& ctx, uint8_t* base, PPCFunc* original, Native native,
        const std::vector<Region>& regions)
    {
        VerifyRegions(name, ctx, base, original, native, regions.data(), regions.size());
    }
}
