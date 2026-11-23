#pragma once

#include <string>
#include <vector>

struct ProbeSample {
    float hostFps = 0;
    float receivedFps = 0;
    float decodedFps = 0;
    float decodeMs = 0;
    float renderMs = 0;
    float cpuPercent = 0;
    float cpuMHz = 0;
    float gpuMHz = 0;
    float ramMHz = 0;
    float decoderMHz = 0;
    float rttMs = 0;
    float dropped = 0;
};

enum class ProbeState { Idle, Running, Done };

// Holds the live samples for one stream test. The view fills it.
class LinkProbe {
  public:
    void start(int seconds);
    void push(const ProbeSample& sample, const std::string& live, const std::string& logLine);
    void finish(const std::string& summary);

    ProbeState state() const { return probeState; }
    bool expired() const;
    float elapsedSeconds() const;
    int durationSeconds() const { return duration; }
    const std::vector<ProbeSample>& samples() const { return probeSamples; }
    const std::string& status() const { return statusText; }

  private:
    ProbeState probeState = ProbeState::Idle;
    std::vector<ProbeSample> probeSamples;
    std::string statusText;
    int duration = 0;
    long long startedMs = 0;
    long long deadlineMs = 0;
};
