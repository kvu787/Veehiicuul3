#include "DisplayCorrelation.h"
#include <iostream>
#include <stdexcept>
void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
int main()
{
    try
    {
        constexpr std::uint64_t frequency=10000000;
        const std::array<DisplayClockAnchor,3> anchors{{{1000000,1000020,7000000},{1200000,1200020,7020000},{1400000,1400020,7040000}}};
        DisplayFrameSubmission first{.frame=1,.swapChain=123,.clockFirstQpc=1000000,.clockLastQpc=1000020,.presentFirstQpc=1000100,.presentEndQpc=1000200,.gameInputTime=7000000,.thread=42,.inputCount=1,.accepted=true};
        first.inputs[0]={.generation=1,.reading=6999000,.serial=1,.sampled=6999990,.sources=1};
        auto second=first; second.frame=2; second.clockFirstQpc=1200000; second.clockLastQpc=1200020; second.presentFirstQpc=1200100; second.presentEndQpc=1200200; second.gameInputTime=7020000; second.inputs[0].sampled=7019990;
        std::array frames{first,second};
        std::array completions{DisplayCompletion{1000150,1100010,123,42,1,true,false},DisplayCompletion{1200150,1300010,123,42,1,true,false}};
        const auto correlate=[&](const DisplayCaptureHealth& health=DisplayCaptureHealth{}) { return FinalizeDisplayCapture(frames,completions,anchors,frequency,health); };
        auto result=correlate();
        Require(result.reliable && result.readings.size()==2 && result.readings[0].duration==11000,"Exact displayed-frame reading duration failed.");
        Require(result.readings[0].firstDisplay && !result.readings[1].firstDisplay,"A repeated reading must have one first displayed sample.");
        second.inputs[0].generation=2; frames[1]=second; result=correlate(); Require(result.readings[1].firstDisplay,"Device/focus generation must prevent stale reading identity reuse.");
        frames[1].inputs[0].generation=1;
        const auto laterDisplay=completions[1].displayQpc; completions[1].displayQpc=completions[0].displayQpc;
        completions[1].presentQpc=1050050; auto sameEndpointFrame=frames[1]; frames[1].presentFirstQpc=1050000; frames[1].presentEndQpc=1050100; frames[1].clockFirstQpc=1000000; frames[1].clockLastQpc=1000020; frames[1].gameInputTime=7000000; frames[1].inputs[0].sampled=6999990;
        result=correlate(); Require(result.reliable && result.readings[0].firstDisplay && !result.readings[1].firstDisplay,"One display timestamp shared by two frames must still count one first reading.");
        frames[1]=sameEndpointFrame; completions[1].displayQpc=laterDisplay; completions[1].presentQpc=1200150;
        const std::array reversedFrames{frames[1],frames[0]}; result=FinalizeDisplayCapture(reversedFrames,completions,anchors,frequency,{}); Require(!result.reliable && result.readings.empty(),"Unordered submissions must fail closed instead of producing guessed matches.");
        completions[0].displayed=false; result=correlate(); Require(result.readings.size()==1 && result.readings[0].firstDisplay && result.discarded==1,"Discarded presentations must not produce durations or consume first-display identity.");
        completions[0].displayed=true; completions[0].lost=true; result=correlate(); Require(result.readings.size()==1,"A decoder-lost presentation must be excluded."); completions[0].lost=false;
        completions[0].thread=43; result=correlate(); Require(result.unmatched==1 && result.readings.size()==1,"Another Present thread must not be guessed into a match."); completions[0].thread=42;
        completions[0].swapChain=999; result=correlate(); Require(result.unmatched==1,"Another swap chain must not match."); completions[0].swapChain=123;
        completions[0].presentQpc=frames[0].presentFirstQpc-1; result=correlate(); Require(result.unmatched==1,"The clock observation is not the exact Present interval."); completions[0].presentQpc=1000150;
        completions[0].displayQpc=0; result=correlate(); Require(result.readings.size()==1,"A missing display endpoint must never substitute CPU/Present time."); completions[0].displayQpc=1100010;
        for(unsigned kind=0;kind<8;++kind)
        {
            DisplayCaptureHealth health;
            switch(kind) { case 0:health.eventsLost=1;break;case 1:health.buffersLost=1;break;case 2:health.submissionsLost=1;break;case 3:health.completionsLost=1;break;case 4:health.decoderWarnings=1;break;case 5:health.decoderOverflows=1;break;case 6:health.inputLoss=true;break;default:health.lossStatisticsKnown=false; }
            result=correlate(health); Require(!result.reliable && result.readings.empty(),"Final loss/unknown statistics must invalidate every provisional duration.");
        }
        frames[0].inputOverflow=true; result=correlate(); Require(!result.reliable && result.readings.empty(),"Consumed-reading overflow must fail closed."); frames[0].inputOverflow=false;
        const std::array duplicate{completions[0],completions[0]}; result=FinalizeDisplayCapture(frames,duplicate,anchors,frequency,{}); Require(!result.reliable && result.readings.empty(),"Duplicate completion must not double-count a frame.");
        auto overlapping=first; overlapping.frame=first.frame+1;
        const std::array overlap{first,overlapping}; result=FinalizeDisplayCapture(overlap,completions,anchors,frequency,{}); Require(!result.reliable && result.ambiguous==1,"Ambiguous Present intervals must fail closed.");
        auto badAnchors=anchors; badAnchors[2].microseconds+=500; result=FinalizeDisplayCapture(frames,completions,badAnchors,frequency,{}); Require(!result.reliable && result.readings.empty(),"Clock discontinuity must invalidate the capture.");
        result=FinalizeDisplayCapture(frames,completions,std::span(anchors).first(1),frequency,{}); Require(result.readings.empty(),"A display endpoint needs a following clock observation.");
        const auto reverse=MapClockTimestamp(anchors[1],1100010,frequency); Require(reverse && reverse->microseconds==7010000,"Reverse clock mapping must preserve epoch and precision.");
        std::cout<<"PASS: exact Present identity, finalized loss checks, two-sided clock validation, discarded/lost/missing/duplicate/ambiguous exclusion and first displayed reading identity.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
