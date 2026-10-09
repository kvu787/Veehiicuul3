#include "DisplayCorrelation.h"
#include "ConsumedDisplayInputs.h"
#include <iostream>
#include <stdexcept>
namespace
{
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
constexpr std::uint64_t Frequency=10000000;
DisplayFrameSubmission Frame(std::uint64_t id,std::uint64_t first,std::uint64_t last,std::uint32_t thread=42,std::uint64_t swapChain=123)
{
    DisplayFrameSubmission frame{.frame=id,.swapChain=swapChain,.clockFirstQpc=first-10,.clockLastQpc=first-10,
        .presentFirstQpc=first,.presentEndQpc=last,.gameInputTime=(first-10)/10,.thread=thread,.inputCount=1,.accepted=true};
    frame.inputs[0]={.device=0,.generation=1,.reading=100,.serial=1,.sampled=110,.sources=1}; return frame;
}
}
int main()
{
    try
    {
        // Synthetic reference SessionValidation protocol: reading 100 us,
        // sample 110 us, Present 1200 QPC, display 1500 QPC -> 150 us / 50 us.
        // Added surrounding anchors and exact call bounds are fixture data,
        // never observed input/display latency on the current application.
        const std::array anchors{DisplayClockAnchor{1100,1100,110},DisplayClockAnchor{1800,1800,180},DisplayClockAnchor{2100,2100,210}};
        std::array frames{Frame(1,1190,1210),Frame(2,1590,1610)};
        std::array events{DisplayCompletion{1200,1500,123,42,1,true,false},DisplayCompletion{1600,1900,123,42,1,true,false}};
        auto result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.reliable && result.readings.size()==2 && result.readings[0].duration==50 && result.readings[1].duration==90,"Reference-protocol epoch conversion and exact 50/90 us fixture durations failed.");
        Require(result.readings[0].firstDisplay && !result.readings[1].firstDisplay,"The first eligible display, not decoder delivery order, owns the reading sample.");
        // Reproduce the reported Menu+loss exit after earlier eligible samples:
        // the pending frame is discarded, so capture health cannot depend on it.
        for(const auto loss:std::array<std::pair<unsigned,unsigned>,3>{{{1,0},{0,1},{1,1}}})
        {
            DisplayInputLossLatch captureLoss; ConsumedDisplayInputs pending;
            pending.Add({0,1,130,2,140,1});
            const InputActions::Edge menu{32,0,145};
            captureLoss.Report(loss.first!=0 || loss.second!=0);
            if(menu.pressed&32u) pending.Clear(); // EndDrive clears it without another submission.
            DisplayCaptureHealth exitHealth; captureLoss.Apply(exitHealth);
            const auto exited=FinalizeDisplayCapture(frames,events,anchors,Frequency,exitHealth);
            Require(!pending.HasLoss() && pending.Take(3).inputCount==0 && exitHealth.inputLoss && !exited.reliable && exited.readings.empty(),"Menu plus dropped edges/callback failure must invalidate earlier samples even when no loss-bearing frame is submitted.");
        }
        DisplayInputLossLatch overflowLoss; ConsumedDisplayInputs overflow;
        overflow.Lost(); overflowLoss.Report(overflow.HasLoss()); overflow.Clear(); DisplayCaptureHealth overflowHealth; overflowLoss.Apply(overflowHealth);
        result=FinalizeDisplayCapture(frames,events,anchors,Frequency,overflowHealth);
        Require(!result.reliable && result.readings.empty(),"A focus/mode/shutdown clear must not erase pending-frame loss already latched for the capture.");
        DisplayInputLossLatch finalReaderLoss; DisplayCaptureHealth finalHealth; finalReaderLoss.Apply(finalHealth,true);
        result=FinalizeDisplayCapture(frames,events,anchors,Frequency,finalHealth);
        Require(!result.reliable && result.readings.empty(),"The final callback-loss read must invalidate previous samples without any additional simulation or rendering step.");
        const std::array reordered{events[1],events[0]}; auto outOfOrder=FinalizeDisplayCapture(frames,reordered,anchors,Frequency,{});
        Require(outOfOrder.reliable && outOfOrder.readings.size()==2 && outOfOrder.readings[0].firstDisplay && !outOfOrder.readings[1].firstDisplay,"Late/reordered ETW delivery must not alter exact reading identity.");
        frames[0].accepted=false; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.frames[0].status=="PresentNotAccepted" && result.readings.size()==1 && result.readings[0].firstDisplay,"Failed or DXGI_STATUS_OCCLUDED Presents must not emit a duration or consume first-display identity."); frames[0].accepted=true;
        events[0].displayed=false; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.frames[0].status=="Discarded" && result.readings.size()==1,"Discarded/occluded frames need an eligible later display, never a guessed endpoint."); events[0].displayed=true;
        events[0].displayQpc=events[0].presentQpc-1; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.frames[0].status=="MissingDisplayEndpoint" && result.readings.size()==1,"A display before its Present must be excluded."); events[0].displayQpc=1500;
        frames[0].inputs[1]=frames[0].inputs[0]; frames[0].inputCount=2; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(!result.reliable && result.readings.empty() && result.invalidInputs==1,"Duplicate input identities inside one frame must fail closed instead of double-counting first samples."); frames[0].inputCount=1;
        frames[0].inputs[0].generation=0; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(!result.reliable && result.readings.empty(),"A fabricated generation must not enter measurements."); frames[0].inputs[0].generation=1;
        for(const auto sources:{0u,4u}) { frames[0].inputs[0].sources=sources; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{}); Require(!result.reliable && result.readings.empty(),"Unconsumed or unknown input source flags must fail closed."); } frames[0].inputs[0].sources=1;
        frames[1].inputs[0].device=1; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.readings[0].firstDisplay && result.readings[1].firstDisplay,"Two devices with the same original timestamp retain independent identity."); frames[1].inputs[0].device=0;
        frames[1].inputs[0].generation=2; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.readings[0].firstDisplay && result.readings[1].firstDisplay,"Reconnection/focus generations cannot merge stale readings."); frames[1].inputs[0].generation=1;
        frames[0].inputs[0].sampled=frames[0].gameInputTime+1; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,{});
        Require(result.readings.size()==1 && result.invalidClocks==1,"A reading consumed after the recorded clock observation cannot be assigned to that frame."); frames[0].inputs[0].sampled=110;
        DisplayCaptureHealth capacity; capacity.capacityExceeded=true; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,capacity);
        Require(!result.reliable && result.readings.empty(),"Capacity loss invalidates every otherwise eligible fixture duration.");
        DisplayCaptureHealth shutdown; shutdown.error=5; result=FinalizeDisplayCapture(frames,events,anchors,Frequency,shutdown);
        Require(!result.reliable && result.readings.empty(),"Start/provider/stop errors invalidate all measurement rows.");
        const std::array overlapAnchors{DisplayClockAnchor{1100,1100,110},DisplayClockAnchor{1800,1800,180}};
        // The short intervening call must not hide an earlier overlapping call.
        const std::array overlap{Frame(1,1200,1650),Frame(2,1250,1300,99,456),Frame(3,1400,1700)};
        const std::array overlapEvents{DisplayCompletion{1500,1750,123,42,1,true,false}};
        result=FinalizeDisplayCapture(overlap,overlapEvents,overlapAnchors,Frequency,{});
        Require(!result.reliable && result.ambiguous==1 && result.readings.empty() && result.frames[0].status=="AmbiguousPresent" && result.frames[2].status=="AmbiguousPresent","An intervening expired interval must not hide ambiguous thread/swap-chain identity.");
        auto distinct=overlap; distinct[2].thread=77;
        result=FinalizeDisplayCapture(distinct,overlapEvents,overlapAnchors,Frequency,{});
        Require(result.reliable && result.readings.size()==1 && result.readings[0].frameIndex==0,"Overlapping calls on another thread must retain the unique earlier match.");
        distinct[2].thread=42; distinct[2].swapChain=789;
        result=FinalizeDisplayCapture(distinct,overlapEvents,overlapAnchors,Frequency,{});
        Require(result.reliable && result.readings.size()==1 && result.readings[0].frameIndex==0,"Overlapping calls on another swap chain must not obscure the unique earlier match.");
        auto equal=frames; equal[1].presentFirstQpc=equal[0].presentFirstQpc; equal[1].presentEndQpc=equal[0].presentEndQpc; equal[1].clockFirstQpc=equal[0].clockFirstQpc; equal[1].clockLastQpc=equal[0].clockLastQpc; equal[1].gameInputTime=equal[0].gameInputTime; equal[1].thread=77;
        const std::array equalEvents{events[0],DisplayCompletion{1200,1500,123,77,1,true,false}};
        result=FinalizeDisplayCapture(equal,equalEvents,anchors,Frequency,{});
        Require(result.reliable && result.readings.size()==2 && result.readings[0].firstDisplay && !result.readings[1].firstDisplay,"Equal QPC Present starts on different threads must remain unique, with one shared first reading endpoint.");
        auto missing=events; missing[0].displayQpc=0; missing[1].displayQpc=0;
        result=FinalizeDisplayCapture(frames,missing,anchors,Frequency,{}); Require(result.readings.empty(),"Locked/unsupported paths without real display timestamps never substitute Present completion.");
        result=FinalizeDisplayCapture(frames,events,std::span(anchors).first(1),Frequency,{}); Require(result.readings.empty(),"A stop before a following clock anchor excludes unbracketed tail endpoints.");
        const std::array reverseAnchors{anchors[1],anchors[0],anchors[2]}; result=FinalizeDisplayCapture(frames,events,reverseAnchors,Frequency,{});
        Require(!result.reliable && result.readings.empty(),"Unordered clock observations must be rejected before binary-search association.");
        std::cout<<"PASS: synthetic reference-protocol associations; overlapping/equal-time thread/swap-chain uniqueness; failed/occluded/missing endpoints; malformed/duplicate inputs; device generations; reordered delivery; final capacity/error and tail-anchor rejection. No observed latency is claimed.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
