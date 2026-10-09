#pragma once
#include "DisplayTiming.h"
#include <map>
#include <set>
#include <atomic>
#include <tuple>
#include <vector>
#include <string_view>

struct DisplayCompletion
{
    std::uint64_t presentQpc{}, displayQpc{}, swapChain{};
    std::uint32_t thread{}, mode{};
    bool displayed{}, lost{};
};
struct DisplayCaptureHealth
{
    std::uint64_t eventsLost{},buffersLost{},submissionsLost{},completionsLost{},decoderWarnings{},decoderOverflows{};
    std::uint32_t error{};
    bool capacityExceeded{},inputLoss{},lossStatisticsKnown=true;
    bool Reliable() const noexcept
    { return !error && lossStatisticsKnown && !eventsLost && !buffersLost && !submissionsLost && !completionsLost && !decoderWarnings && !decoderOverflows && !capacityExceeded && !inputLoss; }
};
// Capture-wide state survives discarded frame buffers and immediate mode exits.
class DisplayInputLossLatch
{
public:
    void Report(bool loss=true) noexcept { if(loss) lost_.store(true,std::memory_order_release); }
    void Apply(DisplayCaptureHealth& health,bool callbackLoss=false) const noexcept
    { health.inputLoss|=callbackLoss || lost_.load(std::memory_order_acquire); }
private:
    std::atomic<bool> lost_{};
};
struct DisplayFrameResult
{
    DisplayCompletion completion;
    std::optional<DisplayTimestamp> display;
    std::string_view status="NoDisplayEvent";
};
struct DisplayReadingResult
{
    std::size_t frameIndex{},inputIndex{};
    std::uint64_t duration{};
    bool firstDisplay{};
};
struct DisplayCorrelationResult
{
    std::vector<DisplayFrameResult> frames;
    std::vector<DisplayReadingResult> readings;
    std::uint64_t unmatched{},ambiguous{},discarded{},invalidClocks{},invalidInputs{};
    bool reliable{};
};

// Called only after the trace is stopped and all loss statistics are final.
// No preliminary/provisional reading durations escape to UI or CSV output.
inline DisplayCorrelationResult FinalizeDisplayCapture(std::span<const DisplayFrameSubmission> frames,
    std::span<const DisplayCompletion> completions,std::span<const DisplayClockAnchor> anchors,
    std::uint64_t frequency,const DisplayCaptureHealth& health)
{
    DisplayCorrelationResult result; result.frames.resize(frames.size()); result.reliable=health.Reliable();
    bool validSubmissions=true;
    for(std::size_t i=0;i<frames.size();++i)
        if(frames[i].presentEndQpc<frames[i].presentFirstQpc || (i && (frames[i].frame<=frames[i-1].frame || frames[i].presentFirstQpc<frames[i-1].presentFirstQpc))) validSubmissions=false;
    if(!validSubmissions)
    { result.reliable=false; for(auto& frame:result.frames) frame.status="InvalidSubmission"; return result; }
    // Validate partitioning before any binary search over clock observations.
    bool validAnchors=frequency && anchors.size()>=2;
    for(std::size_t i=0;i<anchors.size();++i)
    {
        const auto& anchor=anchors[i]; const auto mapped=MapClockTimestamp(anchor,anchor.lastQpc,frequency);
        if(!mapped) { ++result.invalidClocks; validAnchors=false; continue; }
        if(i==0) continue;
        const auto& previous=anchors[i-1]; const auto midpoint=anchor.firstQpc+(anchor.lastQpc-anchor.firstQpc)/2;
        const auto check=MapClockTimestamp(previous,midpoint,frequency);
        const auto difference=check ? (check->microseconds>anchor.microseconds ? check->microseconds-anchor.microseconds : anchor.microseconds-check->microseconds) : UINT64_MAX;
        if(anchor.firstQpc<previous.lastQpc || !check || double(difference)>check->uncertaintyMicroseconds+mapped->uncertaintyMicroseconds+2)
        { ++result.invalidClocks; validAnchors=false; }
    }
    if(!validAnchors) result.reliable=false;
    std::vector<unsigned> matches(frames.size());
    std::vector<std::uint64_t> prefixEnd(frames.size());
    for(std::size_t i=0;i<frames.size();++i) prefixEnd[i]=std::max(frames[i].presentEndQpc,i ? prefixEnd[i-1] : 0);
    for(const auto& completion:completions)
    {
        const auto end=std::upper_bound(frames.begin(),frames.end(),completion.presentQpc,
            [](auto timestamp,const auto& frame) { return timestamp<frame.presentFirstQpc; });
        std::size_t found=frames.size(); unsigned candidates=0;
        for(auto iterator=end;iterator!=frames.begin();)
        {
            --iterator;
            const auto index=static_cast<std::size_t>(iterator-frames.begin());
            // Another thread can have a long Present around a later short call.
            if(prefixEnd[index]<completion.presentQpc) break;
            if(MatchesDisplayFrame(*iterator,completion.presentQpc,completion.thread,completion.swapChain))
            {
                if(candidates) { result.frames[found].status="AmbiguousPresent"; result.frames[index].status="AmbiguousPresent"; }
                found=index; ++candidates;
            }
        }
        if(!candidates) { ++result.unmatched; continue; }
        if(candidates!=1) { ++result.ambiguous; result.reliable=false; continue; }
        auto& item=result.frames[found]; item.completion=completion;
        if(++matches[found]!=1) { item.status="DuplicateDisplayEvent"; result.reliable=false; continue; }
        if(!frames[found].accepted) { item.status="PresentNotAccepted"; continue; }
        if(completion.lost) { item.status="DecoderLostPresent"; continue; }
        if(!completion.displayed) { item.status="Discarded"; ++result.discarded; continue; }
        if(!completion.displayQpc || completion.displayQpc<completion.presentQpc) { item.status="MissingDisplayEndpoint"; continue; }
        if(validAnchors) item.display=BracketDisplayTimestamp(anchors,completion.displayQpc,frequency);
        if(!item.display) { item.status="InvalidClock"; ++result.invalidClocks; continue; }
        item.status="Displayed";
    }
    using ReadingKey=std::tuple<std::uint16_t,std::uint64_t,std::uint64_t>;
    std::map<ReadingKey,std::pair<std::uint64_t,std::uint64_t>> firstShown;
    for(std::size_t i=0;i<frames.size();++i)
    {
        if(frames[i].inputOverflow || frames[i].inputCount>MaximumDisplayInputs) result.reliable=false;
        const auto& item=result.frames[i]; if(item.status!="Displayed" || !item.display) continue;
        std::set<ReadingKey> inputKeys;
        for(std::size_t j=0;j<std::min<std::size_t>(frames[i].inputCount,MaximumDisplayInputs);++j)
        {
            const auto& input=frames[i].inputs[j]; const auto duration=ReadingToDisplayDuration(input,frames[i],*item.display);
            const auto key=ReadingKey{input.device,input.generation,input.reading};
            if(!input.generation || !input.sources || (input.sources&~3u) || !inputKeys.insert(key).second)
            { ++result.invalidInputs; result.reliable=false; continue; }
            if(!duration) { ++result.invalidClocks; continue; }
            const auto endpoint=std::pair{item.completion.displayQpc,frames[i].frame};
            auto [entry,inserted]=firstShown.emplace(key,endpoint);
            if(!inserted) entry->second=std::min(entry->second,endpoint);
            result.readings.push_back({i,j,*duration,false});
        }
    }
    for(auto& reading:result.readings)
    {
        const auto& input=frames[reading.frameIndex].inputs[reading.inputIndex];
        const auto key=ReadingKey{input.device,input.generation,input.reading};
        reading.firstDisplay=firstShown.at(key)==std::pair{result.frames[reading.frameIndex].completion.displayQpc,frames[reading.frameIndex].frame};
    }
    if(!result.reliable) result.readings.clear();
    return result;
}
