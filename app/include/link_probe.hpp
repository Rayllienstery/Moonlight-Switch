#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ProbeSample {
    float mbps = 0;
    float rttMs = 0;
    float jitterMs = 0;
    float lossPercent = 0;
    float cpuPercent = 0;
    float cpuMHz = 0;
    float gpuMHz = 0;
    float ramMHz = 0;
};

enum class ProbeState { Idle, Running, Done };

// Pushes UDP at the host as fast as the socket will take it, and times TCP
// connects on the GameStream port between bursts.
class LinkProbe {
  public:
    LinkProbe() = default;
    ~LinkProbe();

    LinkProbe(const LinkProbe&) = delete;
    LinkProbe& operator=(const LinkProbe&) = delete;

    void start(const std::string& hostName, const std::string& address, int seconds,
               float bitrateMbps);
    void stop();

    ProbeState state() const;
    std::vector<ProbeSample> samples() const;
    std::string status() const;

  private:
    void run(std::string hostName, std::string address, int seconds, float bitrateMbps);

    std::thread worker;
    std::atomic<bool> stopFlag{false};
    mutable std::mutex mutex;
    ProbeState probeState = ProbeState::Idle;
    std::vector<ProbeSample> probeSamples;
    std::string statusText;
};
