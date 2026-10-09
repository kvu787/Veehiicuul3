#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <algorithm>
#include <span>

inline constexpr std::size_t MaximumDisplayInputs = 128;
struct DisplayFrameInput
{
    std::uint16_t device{};
    std::uint64_t generation{}, reading{}, serial{}, sampled{};
    std::uint32_t sources{}; // 1: current analog/brake; 2: consumed reset edge.
};
struct DisplayClockAnchor { std::uint64_t firstQpc{}, lastQpc{}, microseconds{}; };
struct DisplayFrameSubmission
{
    std::uint64_t frame{}, swapChain{}, clockFirstQpc{}, clockLastQpc{}, presentFirstQpc{}, presentEndQpc{}, gameInputTime{};
    std::uint32_t thread{}, inputCount{};
    bool accepted{}, inputOverflow{};
    std::array<DisplayFrameInput, MaximumDisplayInputs> inputs{};
};
struct DisplayPresentProbe
{
    DisplayFrameSubmission* frame{};
    std::uint64_t (*clock)(void*){};
    void* context{};
};
struct DisplayTimestamp
{
    std::uint64_t microseconds{};
    double uncertaintyMicroseconds{};
};

// Convert relative to a tightly bracketed GameInput clock observation, rather
// than assuming that GameInput and QPC have the same epoch or units.
inline std::optional<DisplayTimestamp> MapClockTimestamp(const DisplayClockAnchor& anchor,
    std::uint64_t displayQpc, std::uint64_t frequency) noexcept
{
    if (!frequency || !anchor.firstQpc || !anchor.microseconds || anchor.lastQpc < anchor.firstQpc) return std::nullopt;
    const auto span = anchor.lastQpc - anchor.firstQpc;
    const double uncertainty = static_cast<double>(span) * 500000.0 / static_cast<double>(frequency) + 1.0;
    if (uncertainty > 100.0) return std::nullopt; // Reject preempted calibration calls.
    const auto midpoint = anchor.firstQpc + span / 2;
    // MSVC's long double has double precision. Keep the large GameInput epoch
    // in integer arithmetic so timestamps above 2^53 retain their low bits.
    const bool before=displayQpc<midpoint;
    const long double delta = std::round(static_cast<long double>(before ? midpoint-displayQpc : displayQpc-midpoint)
        * 1000000.0L / static_cast<long double>(frequency));
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (delta < 0 || delta >= static_cast<long double>(maximum)) return std::nullopt;
    const auto elapsed = static_cast<std::uint64_t>(delta);
    if ((!before && elapsed > maximum-anchor.microseconds) || (before && elapsed>anchor.microseconds)) return std::nullopt;
    return DisplayTimestamp{before ? anchor.microseconds-elapsed : anchor.microseconds+elapsed, uncertainty};
}

inline DisplayClockAnchor ClockAnchor(const DisplayFrameSubmission& frame) noexcept
{ return {frame.clockFirstQpc,frame.clockLastQpc,frame.gameInputTime}; }
inline std::optional<DisplayTimestamp> MapDisplayTimestamp(const DisplayFrameSubmission& frame,
    std::uint64_t displayQpc,std::uint64_t frequency) noexcept
{
    if(displayQpc<frame.clockLastQpc) return std::nullopt;
    return MapClockTimestamp(ClockAnchor(frame),displayQpc,frequency);
}

// A finalized measurement requires observations before AND after its ETW
// endpoint. Their QPC->GameInput estimates must agree within both brackets.
inline std::optional<DisplayTimestamp> BracketDisplayTimestamp(std::span<const DisplayClockAnchor> anchors,
    std::uint64_t displayQpc,std::uint64_t frequency) noexcept
{
    const auto after=std::lower_bound(anchors.begin(),anchors.end(),displayQpc,
        [](const auto& anchor,auto timestamp) { return anchor.firstQpc<timestamp; });
    if(after==anchors.end() || after==anchors.begin()) return std::nullopt;
    const auto before=after-1;
    const auto maximum=(std::numeric_limits<std::uint64_t>::max)();
    const auto maximumGap=frequency>maximum/5 ? maximum : frequency*5;
    if(before->lastQpc>displayQpc || after->firstQpc-displayQpc>maximumGap || displayQpc-before->lastQpc>maximumGap) return std::nullopt;
    const auto left=MapClockTimestamp(*before,displayQpc,frequency),right=MapClockTimestamp(*after,displayQpc,frequency);
    if(!left || !right) return std::nullopt;
    const auto difference=left->microseconds>right->microseconds ? left->microseconds-right->microseconds : right->microseconds-left->microseconds;
    if(double(difference)>left->uncertaintyMicroseconds+right->uncertaintyMicroseconds+2) return std::nullopt;
    const auto uncertainty=std::max(left->uncertaintyMicroseconds,right->uncertaintyMicroseconds)+double(difference);
    if(uncertainty>100) return std::nullopt;
    return DisplayTimestamp{left->microseconds,uncertainty};
}

inline bool MatchesDisplayFrame(const DisplayFrameSubmission& frame, std::uint64_t presentQpc,
    std::uint32_t thread, std::uint64_t swapChain) noexcept
{
    return frame.thread == thread && frame.swapChain == swapChain
        && frame.presentFirstQpc>=frame.clockLastQpc && frame.presentEndQpc>=frame.presentFirstQpc
        && presentQpc >= frame.presentFirstQpc && presentQpc <= frame.presentEndQpc;
}

inline std::optional<std::uint64_t> ReadingToDisplayDuration(const DisplayFrameInput& input,
    const DisplayFrameSubmission& frame, const DisplayTimestamp& display) noexcept
{
    if (!input.reading || input.reading > input.sampled || input.sampled > frame.gameInputTime || display.microseconds < frame.gameInputTime) return std::nullopt;
    return display.microseconds - input.reading;
}
