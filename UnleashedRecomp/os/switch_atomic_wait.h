#pragma once

#if defined(__SWITCH__)

#include <cstdint>

// 32-bit blocking wait/notify on arbitrary addresses, built on Horizon's
// address arbiter (svcWaitForAddress / svcSignalToAddress). Semantics mirror
// std::atomic<uint32_t>::wait / notify: the wait blocks while
// *address == undesired, and wakes on a signal to the same address.
//
// A yield-based spin is NOT a substitute here: on Horizon, yielding never
// runs a lower-priority thread queued on the same core, so a spinner can
// stall the lock owner for whole scheduler quanta (or forever, across
// priorities). Guest code takes RtlEnterCriticalSection on its audio path
// every frame, so those stalls are directly audible.
//
// The wait carries a safety timeout: if a wake is ever lost, the caller's
// retry loop degrades to a 1kHz poll instead of hanging.
//
// libnx syscalls are declared directly instead of pulling in <switch.h>,
// whose Event/Semaphore/BIT definitions collide with this codebase.

extern "C"
{
    uint32_t svcWaitForAddress(void* address, uint32_t arbType, int64_t value, int64_t timeoutNs);
    uint32_t svcSignalToAddress(void* address, uint32_t signalType, int32_t value, int32_t count);
    void svcSleepThread(int64_t nano);
    uint32_t svcSetThreadPriority(uint32_t handle, uint32_t priority);
    uint32_t threadGetCurHandle(void);
}

inline void SwitchAtomicWait32(void* address, uint32_t undesired)
{
    constexpr uint32_t ARBITRATION_WAIT_IF_EQUAL = 2;
    constexpr int64_t SAFETY_TIMEOUT_NS = 1'000'000; // 1ms
    svcWaitForAddress(address, ARBITRATION_WAIT_IF_EQUAL,
                      static_cast<int64_t>(undesired), SAFETY_TIMEOUT_NS);
}

inline void SwitchAtomicNotifyOne32(void* address)
{
    constexpr uint32_t SIGNAL_TYPE_SIGNAL = 0;
    svcSignalToAddress(address, SIGNAL_TYPE_SIGNAL, 0, 1);
}

#endif
