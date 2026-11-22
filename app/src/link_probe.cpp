#include "link_probe.hpp"
#include "diagnostics_log.hpp"
#include "switch_load.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

std::string oneDecimal(double value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << value;
    return out.str();
}

#if !defined(_WIN32)

struct Socket {
    int fd = -1;
    ~Socket() {
        if (fd >= 0)
            close(fd);
    }
};

float connectRttMs(const sockaddr_in& address) {
    Socket socket;
    socket.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket.fd < 0)
        return -1.0f;

    const int flags = fcntl(socket.fd, F_GETFL, 0);
    fcntl(socket.fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in target = address;
    target.sin_port = htons(47984);
    const auto started = std::chrono::steady_clock::now();
    const int rc = ::connect(socket.fd, reinterpret_cast<sockaddr*>(&target), sizeof(target));
    if (rc != 0 && errno != EINPROGRESS)
        return -1.0f;

    if (rc != 0) {
        pollfd wait{};
        wait.fd = socket.fd;
        wait.events = POLLOUT;
        if (poll(&wait, 1, 200) <= 0)
            return -1.0f;
        int error = 0;
        socklen_t length = sizeof(error);
        getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &length);
        if (error != 0 && error != ECONNREFUSED)
            return -1.0f;
    }

    const auto elapsed = std::chrono::steady_clock::now() - started;
    return std::chrono::duration<float, std::milli>(elapsed).count();
}

float medianOf(std::vector<float> values) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 0)
        return (values[mid - 1] + values[mid]) * 0.5f;
    return values[mid];
}

#endif

}  // namespace

LinkProbe::~LinkProbe() { stop(); }

void LinkProbe::stop() {
    stopFlag = true;
    if (worker.joinable())
        worker.join();
}

ProbeState LinkProbe::state() const {
    std::lock_guard<std::mutex> guard(mutex);
    return probeState;
}

std::vector<ProbeSample> LinkProbe::samples() const {
    std::lock_guard<std::mutex> guard(mutex);
    return probeSamples;
}

std::string LinkProbe::status() const {
    std::lock_guard<std::mutex> guard(mutex);
    return statusText;
}

void LinkProbe::start(const std::string& hostName, const std::string& address, int seconds,
                      float bitrateMbps) {
    stop();
    stopFlag = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        probeSamples.clear();
        probeState = ProbeState::Running;
        statusText = "Starting the push to " + address + ".";
    }
    worker = std::thread([this, hostName, address, seconds, bitrateMbps] {
        run(hostName, address, seconds, bitrateMbps);
    });
}

void LinkProbe::run(std::string hostName, std::string address, int seconds, float bitrateMbps) {
#if defined(_WIN32)
    std::lock_guard<std::mutex> guard(mutex);
    probeState = ProbeState::Done;
    statusText = "The link test runs on the Switch.";
    (void)hostName;
    (void)address;
    (void)seconds;
    (void)bitrateMbps;
#else
    const std::string who = hostName.empty() ? address : hostName + " (" + address + ")";
    DiagnosticsLog::write(
        "Link test started for " + who + ". Duration " + std::to_string(seconds) +
        " seconds. The Switch sends 1280-byte UDP packets to port 47999 as fast as the socket "
        "accepts them, and times a TCP connect to port 47984 between bursts. Configured stream "
        "bitrate is " +
        oneDecimal(bitrateMbps) +
        " Mbps. This is Switch to host. A stream is host to Switch.");

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(address.c_str(), "47999", &hints, &resolved) != 0 || resolved == nullptr) {
        const std::string message = "Link test stopped. " + address + " did not resolve.";
        DiagnosticsLog::write(message);
        std::lock_guard<std::mutex> guard(mutex);
        statusText = message;
        probeState = ProbeState::Done;
        return;
    }
    sockaddr_in destination = *reinterpret_cast<sockaddr_in*>(resolved->ai_addr);
    freeaddrinfo(resolved);

    Socket udp;
    udp.fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp.fd < 0) {
        const std::string message = "Link test stopped. The UDP socket could not be opened.";
        DiagnosticsLog::write(message);
        std::lock_guard<std::mutex> guard(mutex);
        statusText = message;
        probeState = ProbeState::Done;
        return;
    }
    const int flags = fcntl(udp.fd, F_GETFL, 0);
    fcntl(udp.fd, F_SETFL, flags | O_NONBLOCK);
    const int buffer = 256 * 1024;
    setsockopt(udp.fd, SOL_SOCKET, SO_SNDBUF, &buffer, sizeof(buffer));

    std::vector<uint8_t> packet(1280, 0x5A);
    std::vector<float> rtts;
    int attempts = 0;
    int failures = 0;
    double bytesTotal = 0;
    float peakMbps = 0;
    float peakCpu = 0;
    int lastCpuMHz = 0;
    int lastGpuMHz = 0;
    int lastRamMHz = 0;
    int lastDecoderMHz = 0;

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(seconds);

    while (!stopFlag && std::chrono::steady_clock::now() < deadline) {
        const auto burstStart = std::chrono::steady_clock::now();
        const auto burstEnd = burstStart + std::chrono::milliseconds(250);
        uint64_t burstBytes = 0;
        bool unreachable = false;

        while (!stopFlag && std::chrono::steady_clock::now() < burstEnd) {
            const ssize_t sent = sendto(udp.fd, packet.data(), packet.size(), 0,
                                        reinterpret_cast<sockaddr*>(&destination),
                                        sizeof(destination));
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == ENETUNREACH || errno == EHOSTUNREACH || errno == ENETDOWN) {
                    unreachable = true;
                    break;
                }
                break;
            }
            burstBytes += static_cast<uint64_t>(sent);
        }

        if (unreachable) {
            const std::string message =
                "Link test stopped. The route to " + address + " is down.";
            DiagnosticsLog::write(message);
            std::lock_guard<std::mutex> guard(mutex);
            statusText = message;
            probeState = ProbeState::Done;
            return;
        }

        const float burstSeconds =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - burstStart).count();
        const float mbps = burstSeconds > 0 ? static_cast<float>(burstBytes * 8.0 / 1e6 / burstSeconds)
                                            : 0;
        bytesTotal += static_cast<double>(burstBytes);
        peakMbps = std::max(peakMbps, mbps);

        const float rtt = connectRttMs(destination);
        attempts++;
        if (rtt < 0)
            failures++;
        else
            rtts.push_back(rtt);

        float jitter = 0;
        if (rtts.size() >= 2) {
            double sum = 0;
            for (size_t i = 1; i < rtts.size(); i++)
                sum += std::fabs(rtts[i] - rtts[i - 1]);
            jitter = static_cast<float>(sum / static_cast<double>(rtts.size() - 1));
        }

        const ConsoleLoad load = SwitchLoad::instance().sample();
        peakCpu = std::max(peakCpu, load.cpuPercent);
        if (load.cpuMHz > 0)
            lastCpuMHz = load.cpuMHz;
        if (load.gpuMHz > 0)
            lastGpuMHz = load.gpuMHz;
        if (load.ramMHz > 0)
            lastRamMHz = load.ramMHz;
        if (load.decoderMHz > 0)
            lastDecoderMHz = load.decoderMHz;

        ProbeSample sample;
        sample.mbps = mbps;
        sample.rttMs = rtts.empty() ? 0 : rtts.back();
        sample.jitterMs = jitter;
        sample.lossPercent = attempts == 0 ? 0 : 100.0f * static_cast<float>(failures) / attempts;
        sample.cpuPercent = load.cpuPercent < 0 ? 0 : load.cpuPercent;
        sample.cpuMHz = static_cast<float>(load.cpuMHz);
        sample.gpuMHz = static_cast<float>(load.gpuMHz);
        sample.ramMHz = static_cast<float>(load.ramMHz);

        const float elapsed =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
        const std::string line =
            "Second " + oneDecimal(elapsed) + ". Push " + oneDecimal(mbps) +
            " Mbps. RTT " + oneDecimal(sample.rttMs) + " ms. Jitter " + oneDecimal(jitter) +
            " ms. Connect timeouts " + std::to_string(failures) + " of " +
            std::to_string(attempts) + ". CPU " +
            (load.cpuPercent < 0 ? std::string("not sampled yet")
                                 : oneDecimal(load.cpuPercent) + "%") +
            " at " + std::to_string(load.cpuMHz) + " MHz. GPU " + std::to_string(load.gpuMHz) +
            " MHz. RAM " + std::to_string(load.ramMHz) + " MHz. NVDEC " +
            std::to_string(load.decoderMHz) + " MHz.";
        DiagnosticsLog::write(line);

        const int shownSecond = static_cast<int>(elapsed);
        const std::string live = "Running " + std::to_string(shownSecond) + " / " +
                                 std::to_string(seconds) + " s. Push " + oneDecimal(mbps) +
                                 " Mbps. RTT " + oneDecimal(sample.rttMs) + " ms. CPU " +
                                 oneDecimal(sample.cpuPercent) + "%.";
        {
            std::lock_guard<std::mutex> guard(mutex);
            probeSamples.push_back(sample);
            statusText = live;
            if (stopFlag)
                break;
        }
    }

    const float totalSeconds =
        std::max(0.001f, std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count());
    const float averageMbps = static_cast<float>(bytesTotal * 8.0 / 1e6 / totalSeconds);
    const float medianRtt = medianOf(rtts);
    float peakRtt = 0;
    for (float rtt : rtts)
        peakRtt = std::max(peakRtt, rtt);
    float jitter = 0;
    if (rtts.size() >= 2) {
        double sum = 0;
        for (size_t i = 1; i < rtts.size(); i++)
            sum += std::fabs(rtts[i] - rtts[i - 1]);
        jitter = static_cast<float>(sum / static_cast<double>(rtts.size() - 1));
    }

    std::ostringstream summary;
    summary << "Link test finished for " << who << " after " << oneDecimal(totalSeconds)
            << " seconds. Average push " << oneDecimal(averageMbps) << " Mbps, peak "
            << oneDecimal(peakMbps) << " Mbps. The configured stream is " << oneDecimal(bitrateMbps)
            << " Mbps. ";
    if (averageMbps + 5.0f >= bitrateMbps)
        summary << "The Switch could push at least that much toward the host. ";
    else
        summary << "The Switch could not push the stream bitrate toward the host, so the Wi-Fi hop is the limit. ";
    summary << "RTT median " << oneDecimal(medianRtt) << " ms, peak " << oneDecimal(peakRtt)
            << " ms, jitter " << oneDecimal(jitter) << " ms. TCP connect timeouts " << failures
            << " of " << attempts << ". CPU peak " << oneDecimal(std::max(0.0f, peakCpu))
            << "% at " << lastCpuMHz << " MHz. The CPU "
            << (peakCpu >= 90.0f ? "hit about 100%." : "did not hit 100%.") << " GPU "
            << lastGpuMHz << " MHz, RAM " << lastRamMHz << " MHz, NVDEC " << lastDecoderMHz
            << " MHz. This push is Switch to host. A stream is host to Switch. A fast push does "
               "not prove the host encoder. If a live stream still shows Bad connection, that "
               "line names the decoder or the GPU. Log: "
            << DiagnosticsLog::path() << ".";

    const std::string text = summary.str();
    DiagnosticsLog::write(text);
    std::lock_guard<std::mutex> guard(mutex);
    statusText = text;
    probeState = ProbeState::Done;
#endif
}
