#include "stream_fault.hpp"
#include "MoonlightSession.hpp"
#include "Settings.hpp"

#include <Limelight.h>

#include <cmath>
#include <sstream>

namespace {

std::string codecName() {
    switch (Settings::instance().video_codec()) {
        case H264:
            return "H.264";
        case H265:
            return "HEVC";
        case AV1:
            return "AV1";
    }
    return "unknown codec";
}

std::string oneDecimal(float value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << value;
    return out.str();
}

const char* pegged(float percent) {
    if (percent < 0)
        return "load was not sampled";
    if (percent >= 90.0f)
        return "hit about 100%";
    return "did not hit 100%";
}

}  // namespace

std::string describeStreamContext() {
    const int bitrate = Settings::instance().bitrate();
    std::ostringstream out;
    out << "Stream settings. " << Settings::instance().fps() << " fps, "
        << codecName() << ", " << oneDecimal(bitrate / 1000.0f)
        << " Mbps, frame buffer " << Settings::instance().frames_queue_size()
        << ". Hardware decode is "
        << (Settings::instance().use_hw_decoding() ? "on" : "off") << ".";
    return out.str();
}

std::string describeStreamFault(const SessionStats& stats, const ConsoleLoad& load,
                                int fps, int bitrateKbps) {
    const float budget = fps > 0 ? 1000.0f / static_cast<float>(fps) : 16.7f;
    const auto& decode = stats.video_decode_stats;
    const auto& render = stats.video_render_stats;
    const float sharpen = render.sharpening_time;
    const float drawMs = render.rendering_time + render.post_processing_time;

    uint32_t rtt = 0;
    uint32_t rttVariance = 0;
    const bool haveRtt = LiGetEstimatedRttInfo(&rtt, &rttVariance);

    const bool networkBehind =
        (decode.current_host_fps > 5.0f &&
         decode.current_received_fps + 1.0f < decode.current_host_fps * 0.9f) ||
        (haveRtt && (rtt > 40 || rttVariance > 15));
    const bool cpuBehind =
        decode.current_decoding_time >= budget * 0.8f ||
        (decode.current_received_fps > 5.0f &&
         decode.current_decoded_fps + 1.0f < decode.current_received_fps * 0.85f) ||
        load.cpuPercent >= 90.0f;
    const bool gpuBehind =
        drawMs >= budget * 0.8f ||
        (decode.current_decoded_fps > 5.0f &&
         render.rendered_fps + 1.0f < decode.current_decoded_fps * 0.85f);

    std::ostringstream out;
    out << "Bad connection. Frame budget " << oneDecimal(budget) << " ms at " << fps
        << " fps, requested " << oneDecimal(bitrateKbps / 1000.0f) << " Mbps.\n";
    out << "Network: host " << oneDecimal(decode.current_host_fps)
        << " FPS, received " << oneDecimal(decode.current_received_fps)
        << " FPS, dropped frames " << decode.network_dropped_frames << ". ";
    if (haveRtt)
        out << "RTT " << rtt << " ms, variance " << rttVariance << " ms. ";
    out << (networkBehind ? "The link did not keep up.\n"
                          : "The link kept up.\n");
    out << "CPU: decode " << oneDecimal(decode.current_decoding_time) << " ms, decoded "
        << oneDecimal(decode.current_decoded_fps) << " FPS. CPU "
        << (load.cpuPercent < 0 ? std::string("load was not sampled")
                                : oneDecimal(load.cpuPercent) + "%")
        << " at " << load.cpuMHz << " MHz, NVDEC " << load.decoderMHz
        << " MHz. The CPU " << pegged(load.cpuPercent) << ". "
        << (cpuBehind ? "The decoder did not keep up.\n" : "The decoder kept up.\n");
    out << "GPU: draw " << oneDecimal(render.rendering_time) << " ms, sharpen "
        << oneDecimal(sharpen) << " ms, together " << oneDecimal(drawMs)
        << " ms, presented " << oneDecimal(render.rendered_fps) << " FPS. GPU "
        << load.gpuMHz << " MHz. "
        << (gpuBehind ? "The GPU did not keep up.\n" : "The GPU kept up.\n");
    out << "RAM " << load.ramMHz << " MHz.";
    return out.str();
}
