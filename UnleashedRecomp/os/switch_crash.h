#pragma once

#if defined(__SWITCH__)

// [Switch] Crash reports. A CPU exception (the libnx user exception handler, after nx_crash_handler.c of the
// battd_nx port) or a lost GPU (VK_ERROR_DEVICE_LOST from a submission, fence wait or present) appends a report to
// crash.log next to the NRO when [Switch] SwitchLog is on (off by default since 1.0.0: no log files), and then
// breaks either way so that Atmosphère writes its own crash report. A lost GPU used to freeze the game instead:
// the present waited forever for the frame the dead channel would never finish.
//
// A CPU report has the faulting pc, lr, far and esr, the registers, the frame-pointer chain, the return addresses
// found on the stack and a dump of it; offsets into the executable are "+0x..." on "[crash]" lines, which
// tools/switch-cpu-profile.py names. A GPU report has the driver's own messages from just before, among them the
// reason the channel was lost, which a release Mesa hands only to a VK_EXT_debug_utils messenger (plume makes one).
namespace os::switch_crash
{
    // Installs plume's device-lost and driver message callbacks. `alsoStderr` (SwitchLog): reports go to crash.log,
    // and driver errors and the reports to stderr.log as well; without it nothing is written.
    void Init(bool alsoStderr);
}

#endif
