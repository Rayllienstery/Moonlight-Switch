#include "link_probe.hpp"
#include "diagnostics_log.hpp"

#include <chrono>

namespace {

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

void LinkProbe::start(int seconds) {
    probeSamples.clear();
    duration = seconds;
    startedMs = nowMs();
    deadlineMs = startedMs + static_cast<long long>(seconds) * 1000;
    probeState = ProbeState::Running;
    statusText = "Connecting to the stream.";
}

void LinkProbe::push(const ProbeSample& sample, const std::string& live, const std::string& logLine) {
    if (probeState != ProbeState::Running)
        return;
    probeSamples.push_back(sample);
    statusText = live;
    DiagnosticsLog::write(logLine);
}

void LinkProbe::finish(const std::string& summary) {
    if (probeState == ProbeState::Done)
        return;
    probeState = ProbeState::Done;
    statusText = summary;
    DiagnosticsLog::write(summary);
}

bool LinkProbe::expired() const {
    return probeState == ProbeState::Running && nowMs() >= deadlineMs;
}

float LinkProbe::elapsedSeconds() const {
    if (startedMs == 0)
        return 0;
    return static_cast<float>(nowMs() - startedMs) / 1000.0f;
}
