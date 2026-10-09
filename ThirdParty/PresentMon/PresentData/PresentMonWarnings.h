#pragma once
#include <atomic>
#include <cstdint>

// Integration adapter: retain upstream diagnostics without its service logger.
inline std::atomic<std::uint64_t> PresentMonWarningCount{};
struct PresentMonWarning
{
    explicit PresentMonWarning(bool count = true) noexcept { if (count) ++PresentMonWarningCount; }
    PresentMonWarning& hr(unsigned long) noexcept { return *this; }
    template<class... Values> PresentMonWarning& pmwatch(const Values&...) noexcept { return *this; }
    operator bool() const noexcept { return false; }
};
#define pmlog_warn(message) PresentMonWarning{}
#define pmlog_error(message) PresentMonWarning{}
#define pmlog_info(message) PresentMonWarning{false}
#define pmlog_dbg(message) PresentMonWarning{false}
