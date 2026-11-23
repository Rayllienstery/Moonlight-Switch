#include "trace_graph.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>

TraceGraph::TraceGraph(std::string title, std::string unit, NVGcolor color)
    : title(std::move(title)), unit(std::move(unit)), color(color) {
    setFocusable(false);
    setGrow(1.0f);
    setHeight(10);
}

void TraceGraph::setSamples(std::vector<float> values) { samples = std::move(values); }

void TraceGraph::setReference(float value) { reference = value; }

void TraceGraph::draw(NVGcontext* vg, float x, float y, float width, float height,
                      brls::Style, brls::FrameContext*) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, std::max(0.0f, width), std::max(0.0f, height), 8);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 170));
    nvgFill(vg);

    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, 16);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, nvgRGBA(220, 220, 220, 255));
    nvgText(vg, x + 10, y + 8, title.c_str(), nullptr);

    const float latest = samples.empty() ? 0.0f : samples.back();
    const std::string value = samples.empty()
                                  ? "waiting"
                                  : fmt::format("{:.1f} {}", latest, unit);
    nvgFontSize(vg, 18);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgText(vg, x + width - 10, y + 8, value.c_str(), nullptr);

    const float left = x + 10;
    const float right = x + width - 10;
    const float top = y + 36;
    const float bottom = y + height - 10;
    if (right <= left || bottom <= top || samples.size() < 2)
        return;

    float peak = reference > 0 ? reference : 0.0f;
    for (float sample : samples)
        peak = std::max(peak, sample);
    if (peak < 1.0f)
        peak = 1.0f;
    peak *= 1.1f;

    auto mapY = [&](float sample) {
        const float t = std::clamp(sample / peak, 0.0f, 1.0f);
        return bottom - t * (bottom - top);
    };

    if (reference >= 0.0f) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, left, mapY(reference));
        nvgLineTo(vg, right, mapY(reference));
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 70));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }

    nvgBeginPath(vg);
    for (size_t i = 0; i < samples.size(); i++) {
        const float px = left + (right - left) * static_cast<float>(i) / static_cast<float>(samples.size() - 1);
        const float py = mapY(samples[i]);
        if (i == 0)
            nvgMoveTo(vg, px, py);
        else
            nvgLineTo(vg, px, py);
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 2);
    nvgStroke(vg);
}
