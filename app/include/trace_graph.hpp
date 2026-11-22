#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

class TraceGraph : public brls::Box {
  public:
    TraceGraph(std::string title, std::string unit, NVGcolor color);

    void setSamples(std::vector<float> values);
    void setReference(float value);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    std::string title;
    std::string unit;
    NVGcolor color;
    std::vector<float> samples;
    float reference = -1.0f;
};
