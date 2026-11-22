#pragma once

#include <string>

// One plain-text log an AI can read later. Each line is a sentence with the
// numbers named next to it.
class DiagnosticsLog {
  public:
    static void write(const std::string& sentence);
    static std::string path();
};
