#pragma once
#include "DisplayTiming.h"
#include "InputActions.h"

// The simulation owns this buffer. It records only readings used by its steps;
// repeated analog snapshots merge, while reset edges retain their own timestamp.
class ConsumedDisplayInputs
{
public:
    void Add(DisplayFrameInput input) noexcept
    {
        for(std::size_t i=0;i<frame_.inputCount;++i)
        {
            auto& existing=frame_.inputs[i];
            if(existing.device==input.device && existing.generation==input.generation && existing.reading==input.reading)
            { existing.sources|=input.sources; existing.sampled=std::min(existing.sampled,input.sampled); return; }
        }
        if(frame_.inputCount==MaximumDisplayInputs) { frame_.inputOverflow=true; return; }
        frame_.inputs[frame_.inputCount++]=input;
    }
    void Lost() noexcept { frame_.inputOverflow=true; }
    bool HasLoss() const noexcept { return frame_.inputOverflow; }
    void ConsumeStep(DisplayFrameInput reading,bool analogUsed,std::span<const InputActions::Edge> edges,std::uint32_t resetButton) noexcept
    {
        if(analogUsed) { reading.sources=1; Add(reading); }
        for(const auto& edge:edges) if(edge.pressed&resetButton)
        { auto reset=reading; reset.reading=edge.timestamp; reset.serial=0; reset.sources=2; Add(reset); }
    }
    DisplayFrameSubmission Take(std::uint64_t frame) noexcept { auto result=frame_; result.frame=frame; Clear(); return result; }
    void Clear() noexcept { frame_.inputCount=0; frame_.inputOverflow=false; }
private:
    DisplayFrameSubmission frame_;
};
