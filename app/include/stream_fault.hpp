#pragma once

#include "switch_load.hpp"

#include <string>

struct SessionStats;

// Names which part of the stream fell behind. The same text is shown on
// screen and appended to the diagnostics log.
std::string describeStreamFault(const SessionStats& stats, const ConsoleLoad& load,
                                int fps, int bitrateKbps);
std::string describeStreamContext();
