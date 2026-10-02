#pragma once

#include <cstdint>

// [Switch] SwitchVerifyEvery (round 12): the verify modes (SwitchVerifyMessageDispatch, SwitchVerifyNativeHotFunctions,
// SwitchVerifyNativeAudio) compare one call in this many and run the native path for the others, so that a verify run
// keeps close to normal speed (round 11 compared every call, and the verify run never got past a loading screen).
// Set in main() before any guest code runs; 1 compares every call.
extern uint32_t g_verifyEvery;

inline bool VerifyThisCall()
{
    if (g_verifyEvery <= 1)
        return true;

    static thread_local uint32_t calls = 0;
    if (++calls < g_verifyEvery)
        return false;

    calls = 0;
    return true;
}
