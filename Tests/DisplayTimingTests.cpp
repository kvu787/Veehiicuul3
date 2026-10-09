#include "DisplayTiming.h"
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}
int main()
{
    try {
        // Deliberately different clock epochs, with a 10 MHz QPC clock.
        DisplayFrameSubmission frame{.frame = 42, .swapChain = 1234, .clockFirstQpc = 1000000000,
            .clockLastQpc = 1000000020, .presentFirstQpc = 1000000020, .presentEndQpc = 1000005000, .gameInputTime = 7000000, .thread = 15};
        const auto display = MapDisplayTimestamp(frame, 1000200010, 10000000);
        Require(display && display->microseconds == 7020000, "QPC epoch and frequency conversion failed");
        Require(display->uncertaintyMicroseconds == 2.0, "Calibration uncertainty incorrect");
        Require(display->microseconds - 6999875 == 20125, "Reading-to-display duration incorrect");
        auto largeEpoch = frame;
        largeEpoch.gameInputTime = 13435841900187963ull;
        const auto largeDisplay = MapDisplayTimestamp(largeEpoch, 1000200010, 10000000);
        Require(largeDisplay && largeDisplay->microseconds == largeEpoch.gameInputTime + 20000,
            "Large GameInput epochs must retain single-microsecond precision");
        const auto singleMicrosecond = MapDisplayTimestamp(largeEpoch, largeEpoch.clockLastQpc, 10000000);
        Require(singleMicrosecond && singleMicrosecond->microseconds == largeEpoch.gameInputTime + 1,
            "A one-microsecond delta must remain exact at large epochs");
        largeEpoch.gameInputTime = (std::numeric_limits<std::uint64_t>::max)() - 20000;
        const auto boundaryDisplay = MapDisplayTimestamp(largeEpoch, 1000200010, 10000000);
        Require(boundaryDisplay && boundaryDisplay->microseconds == (std::numeric_limits<std::uint64_t>::max)(),
            "The largest representable mapped timestamp must not be mistaken for overflow");
        DisplayFrameInput input{.reading = 6999875, .serial = 1, .sampled = 6999990};
        Require(ReadingToDisplayDuration(input, frame, *display) == 20125, "Valid input timeline was rejected");
        input.reading = input.sampled + 1;
        Require(!ReadingToDisplayDuration(input, frame, *display), "Future reading at sample time was accepted");
        input.reading = 6999875; input.sampled = frame.gameInputTime + 1;
        Require(!ReadingToDisplayDuration(input, frame, *display), "Sampling after Present was accepted");
        input.sampled = 6999990;
        Require(!ReadingToDisplayDuration(input, frame, DisplayTimestamp{6999999, 0}), "Display before Present was accepted");
        Require(MatchesDisplayFrame(frame, 1000001000, 15, 1234), "Matching presentation was rejected");
        Require(!MatchesDisplayFrame(frame, 1000000010, 15, 1234), "Event before Present was accepted");
        Require(!MatchesDisplayFrame(frame, 1000005001, 15, 1234), "Event after Present was accepted");
        Require(!MatchesDisplayFrame(frame, 1000001000, 16, 1234), "Another thread was matched");
        Require(!MatchesDisplayFrame(frame, 1000001000, 15, 4321), "Another swap chain was matched");
        Require(!MapDisplayTimestamp(frame, 999999999, 10000000), "Display before anchor was accepted");
        Require(!MapDisplayTimestamp(frame, 1000200010, 0), "Zero-frequency clock was accepted");
        frame.clockLastQpc = frame.clockFirstQpc - 1;
        Require(!MapDisplayTimestamp(frame, 1000200010, 10000000), "Reversed clock bracket was accepted");
        frame.clockLastQpc = frame.clockFirstQpc + 5000;
        Require(!MapDisplayTimestamp(frame, 1000200010, 10000000), "Preempted calibration was accepted");
        frame.clockLastQpc = frame.clockFirstQpc;
        frame.gameInputTime = (std::numeric_limits<std::uint64_t>::max)();
        Require(!MapDisplayTimestamp(frame, 1000200010, 10000000), "Overflowing conversion was accepted");
        Require(!MapClockTimestamp({0,0,100},1,10000000),"A failed QPC observation must not invent an epoch");
        const auto reverse=MapClockTimestamp({1000,1000,200},500,10000000);
        Require(reverse && reverse->microseconds==150,"Different clock epochs must support exact backwards relative conversion");
        Require(!MapClockTimestamp({1000,1000,1},1,10000000),"Backwards mapping must reject unsigned underflow");
        const auto fractional=MapClockTimestamp({1000,1000,500},1030,3000000);
        Require(fractional && fractional->microseconds==510,"A non-10-MHz QPC frequency must retain integer microsecond arithmetic");
        const std::array separated{DisplayClockAnchor{1000,1000,100},DisplayClockAnchor{60001000,60001000,6000100}};
        Require(!BracketDisplayTimestamp(separated,1500,10000000),"A display more than five seconds from an anchor must be excluded");
        std::cout << "Display timing conversion and frame correlation checks passed.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
