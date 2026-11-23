#include "switch_load.hpp"
#include "diagnostics_log.hpp"

#include <mutex>

namespace {

std::mutex& loadMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

SwitchLoad& SwitchLoad::instance() {
    static SwitchLoad load;
    return load;
}

#ifndef __SWITCH__

bool SwitchLoad::ensureOpen() { return false; }

ConsoleLoad SwitchLoad::sample() {
    std::lock_guard<std::mutex> guard(loadMutex());
    return {};
}

#else

#include <switch.h>

namespace {

bool openClock(ClkrstSession* session, PcvModuleId id) {
    return R_SUCCEEDED(clkrstOpenSession(session, id, 3));
}

int readClock(ClkrstSession* session, bool open) {
    if (!open)
        return 0;
    u32 hz = 0;
    if (R_FAILED(clkrstGetClockRate(session, &hz)))
        return 0;
    return static_cast<int>(hz / 1000000);
}

}  // namespace

bool SwitchLoad::ensureOpen() {
    if (opened)
        return true;
    if (R_FAILED(clkrstInitialize()))
        return false;
    opened = true;
    return true;
}

ConsoleLoad SwitchLoad::sample() {
    std::lock_guard<std::mutex> guard(loadMutex());
    ConsoleLoad load;

    static ClkrstSession cpuSession{};
    static ClkrstSession gpuSession{};
    static ClkrstSession ramSession{};
    static ClkrstSession decoderSession{};
    static bool cpuOpen = false;
    static bool gpuOpen = false;
    static bool ramOpen = false;
    static bool decoderOpen = false;
    static bool clocksReady = false;
    static bool clocksLogged = false;
    static u64 previousTick = 0;
    static u64 previousIdle = 0;
    static bool havePrevious = false;
    static bool cpuLogged = false;

    if (!clocksReady && ensureOpen()) {
        cpuOpen = openClock(&cpuSession, PcvModuleId_CpuBus);
        gpuOpen = openClock(&gpuSession, PcvModuleId_GPU);
        ramOpen = openClock(&ramSession, PcvModuleId_EMC);
        decoderOpen = openClock(&decoderSession, PcvModuleId_NVDEC);
        clocksReady = true;
    }

    if (clocksReady && !clocksLogged) {
        clocksLogged = true;
        DiagnosticsLog::write(
            std::string("Clock sessions. CPU ") + (cpuOpen ? "open" : "closed") + ", GPU " +
            (gpuOpen ? "open" : "closed") + ", RAM " + (ramOpen ? "open" : "closed") +
            ", NVDEC " + (decoderOpen ? "open" : "closed") + ".");
    }

    load.cpuMHz = readClock(&cpuSession, cpuOpen);
    load.gpuMHz = readClock(&gpuSession, gpuOpen);
    load.ramMHz = readClock(&ramSession, ramOpen);
    load.decoderMHz = readClock(&decoderSession, decoderOpen);

    // IdleTickCount only answers for the core this thread is running on.
    // Reading it here stays on the render thread. Moving a thread across cores
    // from the draw path froze the console.
    const u64 tick = armGetSystemTick();
    u64 idle = 0;
    const bool idleOk = R_SUCCEEDED(
        svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, TickCountInfo_Total));
    if (idleOk && havePrevious && tick > previousTick) {
        const double elapsed = static_cast<double>(tick - previousTick);
        double busy = 1.0 - static_cast<double>(idle - previousIdle) / elapsed;
        if (busy < 0)
            busy = 0;
        if (busy > 1)
            busy = 1;
        load.cpuPercent = static_cast<float>(busy * 100.0);
    }
    if (idleOk) {
        previousTick = tick;
        previousIdle = idle;
        havePrevious = true;
    }

    if (!cpuLogged) {
        cpuLogged = true;
        DiagnosticsLog::write(
            std::string("CPU idle counter ") + (idleOk ? "answered for the render thread core."
                                                       : "did not answer."));
    }
    return load;
}

#endif
