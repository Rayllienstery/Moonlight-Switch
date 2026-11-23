#pragma once

#include "Settings.hpp"
#include "link_probe.hpp"
#include "trace_graph.hpp"

#include <borealis.hpp>
#include <string>

class MoonlightSession;

class StatusNote : public brls::Box {
  public:
    void setText(std::string value) { text = std::move(value); }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    std::string text;
};

class LinkTestView : public brls::Box {
  public:
    explicit LinkTestView(Host host);
    ~LinkTestView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    void start(int seconds);
    void openStream(int seconds, int appId, bool sunshine);
    void finishTest();
    void closeStream();
    void recordSample();
    void syncControls();
    void showGraphs();
    std::string summaryText() const;

    Host host;
    LinkProbe probe;
    MoonlightSession* session = nullptr;
    ProbeState shown = ProbeState::Idle;
    std::string shownStatus;
    bool connecting = false;
    bool cancelConnect = false;
    bool startPending = false;
    bool cpuFromDecoder = false;
    int runningAppId = 0;
    unsigned long long lastSampleMs = 0;
    StatusNote* status = nullptr;
    brls::Button* durations[3] = {};
    brls::Button* stopButton = nullptr;
    TraceGraph* hostGraph = nullptr;
    TraceGraph* decodeGraph = nullptr;
    TraceGraph* cpuGraph = nullptr;
    TraceGraph* cpuClockGraph = nullptr;
    TraceGraph* receivedGraph = nullptr;
    TraceGraph* presentGraph = nullptr;
    TraceGraph* gpuGraph = nullptr;
    TraceGraph* ramGraph = nullptr;
};
