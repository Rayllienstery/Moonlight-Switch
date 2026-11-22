#include "link_test_view.hpp"
#include "Settings.hpp"
#include "diagnostics_log.hpp"

#include <vector>

void StatusNote::draw(NVGcontext* vg, float x, float y, float width, float, brls::Style,
                      brls::FrameContext*) {
    if (text.empty())
        return;
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, 18);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgTextBox(vg, x + 16, y + 8, std::max(0.0f, width - 32), text.c_str(), nullptr);
}

LinkTestView::LinkTestView(Host host) : host(std::move(host)) {
    setAxis(brls::Axis::COLUMN);
    setGrow(1.0f);
    setWidth(brls::View::AUTO);
    setHeight(brls::View::AUTO);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setHeight(72);
    row->setFocusable(false);

    const int seconds[3] = {30, 120, 300};
    const char* labels[3] = {"30 seconds", "2 minutes", "5 minutes"};
    for (int i = 0; i < 3; i++) {
        auto* button = new brls::Button();
        button->setText(labels[i]);
        button->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        button->setGrow(1.0f);
        button->setMargins(8, 6, 4, 6);
        const int duration = seconds[i];
        button->registerClickAction([this, duration](brls::View*) {
            start(duration);
            return true;
        });
        row->addView(button);
        durations[i] = button;
    }
    addView(row);

    stopButton = new brls::Button();
    stopButton->setText("Stop");
    stopButton->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    stopButton->setHeight(56);
    stopButton->setMargins(0, 12, 4, 12);
    stopButton->setState(brls::ButtonState::DISABLED);
    stopButton->registerClickAction([this](brls::View*) {
        probe.stop();
        return true;
    });
    addView(stopButton);

    status = new StatusNote();
    status->setHeight(108);
    status->setFocusable(false);
    const std::string name = this->host.hostname.empty() ? this->host.preferred_address()
                                                         : this->host.hostname;
    status->setText("Host " + name +
                    ". Press a duration to push the link at the maximum the Switch can send. "
                    "The white line on Push is the stream bitrate. Log: " +
                    DiagnosticsLog::path());
    addView(status);

    auto* grid = new brls::Box();
    grid->setAxis(brls::Axis::ROW);
    grid->setGrow(1.0f);
    grid->setFocusable(false);

    auto* left = new brls::Box();
    auto* right = new brls::Box();
    left->setAxis(brls::Axis::COLUMN);
    right->setAxis(brls::Axis::COLUMN);
    left->setGrow(1.0f);
    right->setGrow(1.0f);
    left->setFocusable(false);
    right->setFocusable(false);

    pushGraph = new TraceGraph("Push", "Mbps", nvgRGB(80, 200, 255));
    rttGraph = new TraceGraph("RTT", "ms", nvgRGB(255, 196, 80));
    jitterGraph = new TraceGraph("Jitter", "ms", nvgRGB(255, 140, 80));
    lossGraph = new TraceGraph("Timeouts", "%", nvgRGB(255, 90, 90));
    cpuGraph = new TraceGraph("CPU", "%", nvgRGB(140, 255, 140));
    cpuClockGraph = new TraceGraph("CPU clock", "MHz", nvgRGB(80, 230, 170));
    gpuGraph = new TraceGraph("GPU clock", "MHz", nvgRGB(180, 140, 255));
    ramGraph = new TraceGraph("RAM clock", "MHz", nvgRGB(255, 140, 210));

    pushGraph->setReference(Settings::instance().bitrate() / 1000.0f);
    left->addView(pushGraph);
    right->addView(rttGraph);
    left->addView(jitterGraph);
    right->addView(lossGraph);
    left->addView(cpuGraph);
    right->addView(cpuClockGraph);
    left->addView(gpuGraph);
    right->addView(ramGraph);

    grid->addView(left);
    grid->addView(right);
    addView(grid);
}

LinkTestView::~LinkTestView() { probe.stop(); }

void LinkTestView::start(int seconds) {
    if (host.preferred_address().empty()) {
        status->setText("This host has no address.");
        return;
    }
    shownStatus.clear();
    probe.start(host.hostname, host.preferred_address(), seconds,
                Settings::instance().bitrate() / 1000.0f);
}

void LinkTestView::syncControls() {
    const ProbeState state = probe.state();
    if (state == shown)
        return;
    shown = state;
    const bool running = state == ProbeState::Running;
    const auto buttonState = running ? brls::ButtonState::DISABLED : brls::ButtonState::ENABLED;
    for (brls::Button* button : durations) {
        if (button)
            button->setState(buttonState);
    }
    if (stopButton)
        stopButton->setState(running ? brls::ButtonState::ENABLED : brls::ButtonState::DISABLED);
}

void LinkTestView::draw(NVGcontext* vg, float x, float y, float width, float height,
                        brls::Style style, brls::FrameContext* ctx) {
    syncControls();

    if (probe.state() != ProbeState::Idle) {
        const std::string next = probe.status();
        if (next != shownStatus) {
            shownStatus = next;
            status->setText(next);
        }
    }

    const std::vector<ProbeSample> samples = probe.samples();
    std::vector<float> push, rtt, jitter, loss, cpu, cpuClock, gpu, ram;
    push.reserve(samples.size());
    for (const ProbeSample& sample : samples) {
        push.push_back(sample.mbps);
        rtt.push_back(sample.rttMs);
        jitter.push_back(sample.jitterMs);
        loss.push_back(sample.lossPercent);
        cpu.push_back(sample.cpuPercent);
        cpuClock.push_back(sample.cpuMHz);
        gpu.push_back(sample.gpuMHz);
        ram.push_back(sample.ramMHz);
    }
    pushGraph->setSamples(push);
    rttGraph->setSamples(rtt);
    jitterGraph->setSamples(jitter);
    lossGraph->setSamples(loss);
    cpuGraph->setSamples(cpu);
    cpuClockGraph->setSamples(cpuClock);
    gpuGraph->setSamples(gpu);
    ramGraph->setSamples(ram);

    Box::draw(vg, x, y, width, height, style, ctx);
}
