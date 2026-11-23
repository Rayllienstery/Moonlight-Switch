#include "link_test_view.hpp"
#include "GameStreamClient.hpp"
#include "MoonlightSession.hpp"
#include "diagnostics_log.hpp"
#include "stream_fault.hpp"
#include "switch_load.hpp"

#include <Limelight.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

namespace {

std::string oneDecimal(float value) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << value;
    return out.str();
}

std::string hostLabel(const Host& host) {
    if (!host.hostname.empty())
        return host.hostname + " (" + host.preferred_address() + ")";
    return host.preferred_address();
}

std::string lowerCopy(std::string value) {
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

const AppInfo* findDesktopApp(const AppInfoList& apps) {
    for (const AppInfo& app : apps) {
        const std::string lower = lowerCopy(app.name);
        if (lower == "desktop" || app.name == "Рабочий стол" || app.name == "рабочий стол")
            return &app;
    }
    for (const AppInfo& app : apps) {
        if (lowerCopy(app.name).find("desktop") != std::string::npos)
            return &app;
    }
    return nullptr;
}

}  // namespace

void StatusNote::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style,
                      brls::FrameContext*) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, std::max(0.0f, width), std::max(0.0f, height), 8);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 170));
    nvgFill(vg);

    if (text.empty())
        return;
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, 16);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgTextBox(vg, x + 10, y + 8, std::max(0.0f, width - 20), text.c_str(), nullptr);
}

LinkTestView::LinkTestView(Host host) : host(std::move(host)) {
    setAxis(brls::Axis::COLUMN);
    setGrow(1.0f);
    setWidth(brls::View::AUTO);
    setHeight(brls::View::AUTO);
    setPadding(8, 16, 8, 16);
    setBackground(brls::ViewBackground::NONE);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(56);
    row->setFocusable(false);

    auto* backButton = new brls::Button();
    backButton->setText(brls::getStr("hints/back"));
    backButton->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    backButton->setGrow(1.0f);
    backButton->setMargins(0, 4, 0, 4);
    backButton->registerClickAction([this](brls::View*) {
        finishTest();
        brls::Application::popActivity();
        return true;
    });
    row->addView(backButton);

    const int seconds[3] = {30, 120, 300};
    const char* labels[3] = {"30 seconds", "2 minutes", "5 minutes"};
    for (int i = 0; i < 3; i++) {
        auto* button = new brls::Button();
        button->setText(labels[i]);
        button->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        button->setGrow(1.0f);
        button->setMargins(0, 4, 0, 4);
        const int duration = seconds[i];
        button->registerClickAction([this, duration](brls::View*) {
            start(duration);
            return true;
        });
        row->addView(button);
        durations[i] = button;
    }

    stopButton = new brls::Button();
    stopButton->setText("Stop");
    stopButton->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    stopButton->setGrow(1.0f);
    stopButton->setMargins(0, 4, 0, 4);
    stopButton->setState(brls::ButtonState::DISABLED);
    stopButton->registerClickAction([this](brls::View*) {
        finishTest();
        return true;
    });
    row->addView(stopButton);
    addView(row);

    status = new StatusNote();
    status->setHeight(88);
    status->setMargins(8, 4, 4, 4);
    status->setFocusable(false);
    status->setText("Link test for " + hostLabel(this->host) +
                    ". Pick a duration. A Steam game such as RimWorld is already on the "
                    "desktop, so the test streams Desktop and measures encode, decode, present, "
                    "CPU, GPU, RAM, and the link. Log: " +
                    DiagnosticsLog::path());
    addView(status);

    auto* picture = new brls::Box();
    picture->setGrow(1.0f);
    picture->setFocusable(false);
    addView(picture);

    auto* grid = new brls::Box();
    grid->setAxis(brls::Axis::ROW);
    grid->setHeight(292);
    grid->setFocusable(false);

    auto* left = new brls::Box();
    auto* right = new brls::Box();
    left->setAxis(brls::Axis::COLUMN);
    right->setAxis(brls::Axis::COLUMN);
    left->setGrow(1.0f);
    right->setGrow(1.0f);
    left->setFocusable(false);
    right->setFocusable(false);

    hostGraph = new TraceGraph("Host FPS", "fps", nvgRGB(80, 200, 255));
    decodeGraph = new TraceGraph("Decode", "ms", nvgRGB(255, 196, 80));
    cpuGraph = new TraceGraph("CPU", "%", nvgRGB(140, 255, 140));
    cpuClockGraph = new TraceGraph("CPU clock", "MHz", nvgRGB(80, 230, 170));
    receivedGraph = new TraceGraph("Received", "fps", nvgRGB(255, 140, 80));
    presentGraph = new TraceGraph("Present", "ms", nvgRGB(255, 90, 90));
    gpuGraph = new TraceGraph("GPU clock", "MHz", nvgRGB(180, 140, 255));
    ramGraph = new TraceGraph("RAM clock", "MHz", nvgRGB(255, 140, 210));

    const float targetFps = static_cast<float>(std::max(1, Settings::instance().fps()));
    const float budget = 1000.0f / targetFps;
    hostGraph->setReference(targetFps);
    receivedGraph->setReference(targetFps);
    decodeGraph->setReference(budget);
    presentGraph->setReference(budget);

    for (TraceGraph* graph : {hostGraph, decodeGraph, cpuGraph, cpuClockGraph, receivedGraph,
                              presentGraph, gpuGraph, ramGraph})
        graph->setMargins(4, 4, 4, 4);

    left->addView(hostGraph);
    right->addView(receivedGraph);
    left->addView(decodeGraph);
    right->addView(presentGraph);
    left->addView(cpuGraph);
    right->addView(cpuClockGraph);
    left->addView(gpuGraph);
    right->addView(ramGraph);

    grid->addView(left);
    grid->addView(right);
    addView(grid);
}

LinkTestView::~LinkTestView() { closeStream(); }

void LinkTestView::start(int seconds) {
    if (connecting || startPending || probe.state() == ProbeState::Running)
        return;
    if (host.preferred_address().empty()) {
        status->setText("This host has no address.");
        return;
    }

    cancelConnect = false;
    connecting = true;
    shownStatus.clear();
    status->setText("Connecting to " + hostLabel(host) + ".");

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN, seconds](GSResult<SERVER_DATA> result) {
            ASYNC_RELEASE
            connecting = false;
            if (cancelConnect)
                return;
            if (!result.isSuccess()) {
                const std::string message = "Link test did not connect. " + result.error();
                status->setText(message);
                DiagnosticsLog::write(message);
                return;
            }

            auto server = result.value();
            if (server.currentGame != 0) {
                runningAppId = server.currentGame;
                openStream(seconds, server.currentGame, server.isSunshine());
                return;
            }

            connecting = true;
            status->setText(
                "No Moonlight session is open. A Steam game stays on the desktop, so the test "
                "will stream that screen.");
            const bool sunshine = server.isSunshine();
            ASYNC_RETAIN
            GameStreamClient::instance().applist(
                host, [ASYNC_TOKEN, seconds, sunshine](GSResult<AppInfoList> listResult) {
                    ASYNC_RELEASE
                    connecting = false;
                    if (cancelConnect)
                        return;
                    if (!listResult.isSuccess()) {
                        const std::string message =
                            "The test could not read the Sunshine app list. " + listResult.error();
                        status->setText(message);
                        DiagnosticsLog::write(message);
                        return;
                    }

                    AppInfoList apps = listResult.value();
                    const AppInfo* found = findDesktopApp(apps);
                    if (!found) {
                        const std::string message =
                            "Sunshine has no Desktop app. A Steam game is a window on the PC, "
                            "not a Moonlight session. Add Desktop in Sunshine, then run the test.";
                        status->setText(message);
                        DiagnosticsLog::write(message);
                        return;
                    }
                    const AppInfo desktop = *found;

                    DiagnosticsLog::write(
                        "No Moonlight session was active. A game started from Steam, such as "
                        "RimWorld, is already on the PC desktop, so the test streams the Desktop "
                        "app \"" +
                        desktop.name + "\" id " + std::to_string(desktop.app_id) +
                        ". It does not launch the game.");
                    runningAppId = desktop.app_id;
                    openStream(seconds, desktop.app_id, sunshine);
                });
        });
}

void LinkTestView::openStream(int seconds, int appId, bool sunshine) {
    closeStream();
    const std::string address = GameStreamClient::instance().active_address(host);
    const std::string target = address.empty() ? host.preferred_address() : address;
    session = new MoonlightSession(target, appId);
    probe.start(seconds);
    cpuFromDecoder = false;
    lastSampleMs = 0;

    const std::string opening =
        "Link test started for " + hostLabel(host) + ". Joining the running app " +
        std::to_string(appId) + " at " + target + ". Duration " + std::to_string(seconds) +
        " seconds. " + describeStreamContext() +
        " The picture is the live stream. The PC game stays running when the test stops.";
    DiagnosticsLog::write(opening);
    status->setText("Waiting for the picture from app " + std::to_string(appId) + ".");

    startPending = true;
    ASYNC_RETAIN
    session->start(
        [ASYNC_TOKEN](GSResult<bool> result) {
            ASYNC_RELEASE
            startPending = false;
            if (result.isSuccess())
                return;
            const std::string message = "The stream did not start. " + result.error();
            if (probe.state() == ProbeState::Running)
                probe.finish(message);
            status->setText(message);
            closeStream();
        },
        sunshine);
}

void LinkTestView::closeStream() {
    if (!session)
        return;
    session->stop(0);
    if (startPending) {
        session = nullptr;
        return;
    }
    delete session;
    session = nullptr;
}

void LinkTestView::finishTest() {
    cancelConnect = true;
    connecting = false;
    if (probe.state() == ProbeState::Running)
        probe.finish(summaryText());
    else if (probe.state() == ProbeState::Idle)
        status->setText("Stopped.");
    closeStream();
    if (probe.state() == ProbeState::Done)
        status->setText(probe.status());
}

std::string LinkTestView::summaryText() const {
    const float target = static_cast<float>(std::max(1, Settings::instance().fps()));
    const float budget = 1000.0f / target;
    double hostSum = 0;
    double receivedSum = 0;
    double decodedSum = 0;
    int counted = 0;
    float peakDecode = 0;
    float peakRender = 0;
    float peakCpu = 0;
    float cpuMHz = 0;
    float gpuMHz = 0;
    float ramMHz = 0;
    float decoderMHz = 0;
    float rtt = 0;
    float dropped = 0;

    for (const ProbeSample& sample : probe.samples()) {
        if (sample.hostFps > 1.0f || sample.receivedFps > 1.0f) {
            hostSum += sample.hostFps;
            receivedSum += sample.receivedFps;
            decodedSum += sample.decodedFps;
            counted++;
        }
        peakDecode = std::max(peakDecode, sample.decodeMs);
        peakRender = std::max(peakRender, sample.renderMs);
        peakCpu = std::max(peakCpu, sample.cpuPercent);
        if (sample.cpuMHz > 0)
            cpuMHz = sample.cpuMHz;
        if (sample.gpuMHz > 0)
            gpuMHz = sample.gpuMHz;
        if (sample.ramMHz > 0)
            ramMHz = sample.ramMHz;
        if (sample.decoderMHz > 0)
            decoderMHz = sample.decoderMHz;
        if (sample.rttMs > 0)
            rtt = sample.rttMs;
        dropped = sample.dropped;
    }

    std::ostringstream out;
    out << "Link test finished after " << oneDecimal(probe.elapsedSeconds()) << " seconds on app "
        << runningAppId << ". ";
    if (counted == 0) {
        out << "No encoded frames were measured. The picture did not arrive. ";
    } else {
        const float hostFps = static_cast<float>(hostSum / counted);
        const float receivedFps = static_cast<float>(receivedSum / counted);
        const float decodedFps = static_cast<float>(decodedSum / counted);
        out << "Host encode averaged " << oneDecimal(hostFps) << " fps, target " << oneDecimal(target)
            << " fps. The Switch received " << oneDecimal(receivedFps) << " fps and decoded "
            << oneDecimal(decodedFps) << " fps. ";
        out << (hostFps + 1.0f < target * 0.9f ? "The host encoder fell behind the target. "
                                               : "The host encoder kept up with the target. ");
        out << (receivedFps + 1.0f < hostFps * 0.9f
                    ? "The link delivered fewer frames than the host encoded. "
                    : "The link delivered the encoded frames. ");
    }
    out << "Decode peaked at " << oneDecimal(peakDecode) << " ms. Present and RCAS peaked at "
        << oneDecimal(peakRender) << " ms. The frame budget is " << oneDecimal(budget) << " ms. ";
    out << (peakDecode >= budget * 0.8f ? "Decode used most of the frame budget. "
                                        : "Decode fit in the frame budget. ");
    out << (peakRender >= budget * 0.8f ? "Present and RCAS used most of the frame budget. "
                                        : "Present and RCAS fit in the frame budget. ");
    out << "Dropped frames " << oneDecimal(dropped) << ". RTT " << oneDecimal(rtt) << " ms. CPU peaked at "
        << oneDecimal(peakCpu) << "% at " << oneDecimal(cpuMHz) << " MHz. GPU " << oneDecimal(gpuMHz)
        << " MHz, RAM " << oneDecimal(ramMHz) << " MHz, NVDEC " << oneDecimal(decoderMHz) << " MHz. ";
    if (cpuFromDecoder)
        out << "The CPU graph is decoder occupancy because the idle counter had no reading. ";
    else
        out << (peakCpu >= 90.0f ? "The CPU hit about 100%. " : "The CPU did not hit 100%. ");
    out << "The PC game was left running.";
    return out.str();
}

void LinkTestView::recordSample() {
    if (!session || !session->is_active())
        return;

    const unsigned long long now = LiGetMillis();
    if (lastSampleMs != 0 && now - lastSampleMs < 250)
        return;
    lastSampleMs = now;

    const SessionStats* stats = session->session_stats();
    if (!stats)
        return;

    const ConsoleLoad load = SwitchLoad::instance().sample();
    const auto& decode = stats->video_decode_stats;
    const auto& render = stats->video_render_stats;
    const float budget = 1000.0f / static_cast<float>(std::max(1, Settings::instance().fps()));
    const float renderMs = render.rendering_time + render.post_processing_time + render.sharpening_time;

    uint32_t rtt = 0;
    uint32_t rttVariance = 0;
    LiGetEstimatedRttInfo(&rtt, &rttVariance);

    ProbeSample sample;
    sample.hostFps = decode.current_host_fps;
    sample.receivedFps = decode.current_received_fps;
    sample.decodedFps = decode.current_decoded_fps;
    sample.decodeMs = decode.current_decoding_time;
    sample.renderMs = renderMs;
    sample.cpuMHz = static_cast<float>(load.cpuMHz);
    sample.gpuMHz = static_cast<float>(load.gpuMHz);
    sample.ramMHz = static_cast<float>(load.ramMHz);
    sample.decoderMHz = static_cast<float>(load.decoderMHz);
    sample.rttMs = static_cast<float>(rtt);
    sample.dropped = static_cast<float>(decode.network_dropped_frames);
    if (load.cpuPercent >= 0.0f) {
        sample.cpuPercent = load.cpuPercent;
        cpuFromDecoder = false;
    } else {
        sample.cpuPercent = std::clamp(sample.decodeMs / budget * 100.0f, 0.0f, 100.0f);
        cpuFromDecoder = true;
    }

    const std::string cpuText = cpuFromDecoder ? oneDecimal(sample.cpuPercent) + "% decoder occupancy"
                                               : oneDecimal(sample.cpuPercent) + "%";
    const std::string live =
        "Running " + std::to_string(static_cast<int>(probe.elapsedSeconds())) + " / " +
        std::to_string(probe.durationSeconds()) + " s. Host " + oneDecimal(sample.hostFps) +
        " fps, received " + oneDecimal(sample.receivedFps) + " fps, decode " +
        oneDecimal(sample.decodeMs) + " ms, present " + oneDecimal(sample.renderMs) + " ms. CPU " +
        cpuText + " at " + std::to_string(load.cpuMHz) + " MHz. GPU " + std::to_string(load.gpuMHz) +
        " MHz, RAM " + std::to_string(load.ramMHz) + " MHz, NVDEC " +
        std::to_string(load.decoderMHz) + " MHz. RTT " + std::to_string(rtt) + " ms, drops " +
        std::to_string(decode.network_dropped_frames) + ".";
    const std::string logLine =
        "Second " + oneDecimal(probe.elapsedSeconds()) + ". Host encode " + oneDecimal(sample.hostFps) +
        " fps. Received " + oneDecimal(sample.receivedFps) + " fps. Decoded " +
        oneDecimal(sample.decodedFps) + " fps. Decode " + oneDecimal(sample.decodeMs) +
        " ms. Present and RCAS " + oneDecimal(sample.renderMs) + " ms. Frame budget " +
        oneDecimal(budget) + " ms. CPU " + cpuText + " at " + std::to_string(load.cpuMHz) +
        " MHz. GPU " + std::to_string(load.gpuMHz) + " MHz. RAM " + std::to_string(load.ramMHz) +
        " MHz. NVDEC " + std::to_string(load.decoderMHz) + " MHz. RTT " + std::to_string(rtt) +
        " ms, variance " + std::to_string(rttVariance) + " ms. Dropped frames " +
        std::to_string(decode.network_dropped_frames) + ".";
    probe.push(sample, live, logLine);
}

void LinkTestView::syncControls() {
    const bool running = connecting || startPending || probe.state() == ProbeState::Running;
    if (probe.state() != shown || running != (shown == ProbeState::Running)) {
        shown = running ? ProbeState::Running : probe.state();
        const auto buttonState = running ? brls::ButtonState::DISABLED : brls::ButtonState::ENABLED;
        for (brls::Button* button : durations) {
            if (button)
                button->setState(buttonState);
        }
        if (stopButton)
            stopButton->setState(running ? brls::ButtonState::ENABLED : brls::ButtonState::DISABLED);
    }
}

void LinkTestView::showGraphs() {
    const std::vector<ProbeSample>& samples = probe.samples();
    std::vector<float> host, received, decode, present, cpu, cpuClock, gpu, ram;
    host.reserve(samples.size());
    for (const ProbeSample& sample : samples) {
        host.push_back(sample.hostFps);
        received.push_back(sample.receivedFps);
        decode.push_back(sample.decodeMs);
        present.push_back(sample.renderMs);
        cpu.push_back(sample.cpuPercent);
        cpuClock.push_back(sample.cpuMHz);
        gpu.push_back(sample.gpuMHz);
        ram.push_back(sample.ramMHz);
    }
    hostGraph->setSamples(std::move(host));
    receivedGraph->setSamples(std::move(received));
    decodeGraph->setSamples(std::move(decode));
    presentGraph->setSamples(std::move(present));
    cpuGraph->setSamples(std::move(cpu));
    cpuClockGraph->setSamples(std::move(cpuClock));
    gpuGraph->setSamples(std::move(gpu));
    ramGraph->setSamples(std::move(ram));
}

void LinkTestView::draw(NVGcontext* vg, float x, float y, float width, float height,
                        brls::Style style, brls::FrameContext* ctx) {
    syncControls();

    if (session && session->is_active())
        session->draw(vg, static_cast<int>(width), static_cast<int>(height));

    if (probe.state() == ProbeState::Running) {
        if (probe.expired() || (session && session->is_terminated()))
            finishTest();
        else
            recordSample();
    }

    if (probe.state() != ProbeState::Idle) {
        const std::string& next = probe.status();
        if (next != shownStatus) {
            shownStatus = next;
            status->setText(next);
        }
    }

    showGraphs();
    Box::draw(vg, x, y, width, height, style, ctx);
}
