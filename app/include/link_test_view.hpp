#pragma once

#include "Settings.hpp"
#include "link_probe.hpp"
#include "trace_graph.hpp"

#include <borealis.hpp>
#include <string>

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
    void syncControls();

    Host host;
    LinkProbe probe;
    ProbeState shown = ProbeState::Idle;
    std::string shownStatus;
    StatusNote* status = nullptr;
    brls::Button* durations[3] = {};
    brls::Button* stopButton = nullptr;
    TraceGraph* pushGraph = nullptr;
    TraceGraph* rttGraph = nullptr;
    TraceGraph* jitterGraph = nullptr;
    TraceGraph* lossGraph = nullptr;
    TraceGraph* cpuGraph = nullptr;
    TraceGraph* cpuClockGraph = nullptr;
    TraceGraph* gpuGraph = nullptr;
    TraceGraph* ramGraph = nullptr;
};
