#include "switch_load.hpp"

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

    if (!clocksReady && ensureOpen()) {
        cpuOpen = openClock(&cpuSession, PcvModuleId_CpuBus);
        gpuOpen = openClock(&gpuSession, PcvModuleId_GPU);
        ramOpen = openClock(&ramSession, PcvModuleId_EMC);
        decoderOpen = openClock(&decoderSession, PcvModuleId_NVDEC);
        clocksReady = true;
    }

    load.cpuMHz = readClock(&cpuSession, cpuOpen);
    load.gpuMHz = readClock(&gpuSession, gpuOpen);
    load.ramMHz = readClock(&ramSession, ramOpen);
    load.decoderMHz = readClock(&decoderSession, decoderOpen);

    const u64 tick = armGetSystemTick();
    u64 idle[3] = {};
    bool idleOk = true;
    for (int core = 0; core < 3; core++) {
        if (R_FAILED(svcGetInfo(&idle[core], InfoType_IdleTickCount, INVALID_HANDLE, core)))
            idleOk = false;
    }

    if (idleOk && havePrevious && tick > previousTick) {
        const double elapsed = static_cast<double>(tick - previousTick);
        double busySum = 0;
        for (int core = 0; core < 3; core++) {
            const double idleDelta = static_cast<double>(idle[core] - previousIdle[core]);
            double busy = 1.0 - idleDelta / elapsed;
            if (busy < 0)
                busy = 0;
            if (busy > 1)
                busy = 1;
            busySum += busy;
        }
        load.cpuPercent = static_cast<float>(busySum / 3.0 * 100.0);
    }

    if (idleOk) {
        previousTick = tick;
        for (int core = 0; core < 3; core++)
            previousIdle[core] = idle[core];
        havePrevious = true;
    }

    return load;
}

#endif
