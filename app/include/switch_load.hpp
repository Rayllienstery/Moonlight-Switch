#pragma once

struct ConsoleLoad {
    // Negative until two samples exist.
    float cpuPercent = -1.0f;
    int cpuMHz = 0;
    int gpuMHz = 0;
    int ramMHz = 0;
    int decoderMHz = 0;
};

class SwitchLoad {
  public:
    static SwitchLoad& instance();
    ConsoleLoad sample();

  private:
    SwitchLoad() = default;
    bool ensureOpen();

    bool opened = false;
    bool havePrevious = false;
    unsigned long long previousTick = 0;
    unsigned long long previousIdle[3] = {};
};
