#include "diagnostics_log.hpp"
#include "Settings.hpp"

#include <borealis.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {

std::mutex& logMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()) %
                              1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << milliseconds.count() << "Z";
    return out.str();
}

}  // namespace

std::string DiagnosticsLog::path() {
    const std::string dir = Settings::instance().working_dir();
    if (dir.empty())
        return "stream-diagnostics.log";
    if (dir.back() == '/' || dir.back() == '\\')
        return dir + "stream-diagnostics.log";
    return dir + "/stream-diagnostics.log";
}

void DiagnosticsLog::write(const std::string& sentence) {
    if (sentence.empty())
        return;

    const std::string line = timestamp() + " " + sentence;
    brls::Logger::info("{}", line);

    std::lock_guard<std::mutex> guard(logMutex());
    std::ofstream file(path(), std::ios::app);
    if (!file)
        return;
    file << line << '\n';
    file.flush();
}
