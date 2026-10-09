#include "ConsumedDisplayInputs.h"
#include "DisplayCaptureOptions.h"
#include "Measurement.h"
#include <iostream>
#include <stdexcept>
#include <thread>
void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        ConsumedDisplayInputs consumed;
        for(unsigned step=0;step<8;++step) consumed.Add({1,5,1000,3,1100+step,1});
        consumed.Add({1,5,900,0,1200,2}); // A reset edge carries its callback reading's original timestamp.
        consumed.Add({1,5,1000,3,1300,2});
        consumed.Add({1,6,1000,4,1300,1}); // Reconnected/refocused readings cannot share identity.
        auto frame=consumed.Take(42);
        Require(frame.frame==42 && frame.inputCount==3 && frame.inputs[0].sources==3 && frame.inputs[0].sampled==1100,"Repeated simulation samples must merge while retaining their original timestamp and first consumption time.");
        Require(frame.inputs[1].reading==900 && frame.inputs[1].sources==2 && frame.inputs[2].generation==6,"Reset edges and focus/device generations must retain independent identity.");
        Require(consumed.Take(43).inputCount==0,"Consumed readings must not leak into the next rendered frame.");
        const std::array edges{InputActions::Edge{16,0,910},InputActions::Edge{0,16,920},InputActions::Edge{32,0,930}};
        consumed.ConsumeStep({1,7,1000,5,1200,0},false,edges,16);
        frame=consumed.Take(46); Require(frame.inputCount==1 && frame.inputs[0].reading==910 && frame.inputs[0].sources==2 && frame.inputs[0].serial==0,"A reset consumed during an analog baseline/reset step must retain its original edge timestamp; releases and other buttons are excluded.");
        consumed.ConsumeStep({1,7,1000,5,1200,0},true,{},16);
        frame=consumed.Take(47); Require(frame.inputCount==1 && frame.inputs[0].reading==1000 && frame.inputs[0].sources==1,"Analog readings belong only to steps that use them.");
        for(std::size_t i=0;i<MaximumDisplayInputs+1;++i) consumed.Add({0,1,100+i,1,1000,1});
        Require(consumed.HasLoss(),"Input overflow must be observable before a frame is submitted or cleared by an early exit.");
        frame=consumed.Take(44); Require(frame.inputOverflow && frame.inputCount==MaximumDisplayInputs,"Bounded reading overflow must be explicit rather than dropping identity silently.");
        consumed.Lost(); Require(consumed.Take(45).inputOverflow,"Lost callback edges must mark the corresponding capture frame.");
        SingleProducerQueue<unsigned,4> queue;
        for(unsigned i=0;i<4;++i) Require(queue.Push(i),"Queue must accept its bounded capacity.");
        Require(!queue.Push(5),"A full queue must fail immediately.");
        for(unsigned i=0;i<4;++i) { unsigned value=99; Require(queue.Pop(value) && value==i,"The correlation queue must retain order across saturation."); }
        for(unsigned i=0;i<1000;++i) { unsigned value=0; Require(queue.Push(i) && queue.Pop(value) && value==i,"Ring wraparound must preserve exact frame identity."); }
        SingleProducerQueue<unsigned,256> concurrent;
        std::jthread producer([&](std::stop_token stop) { for(unsigned i=0;i<50000;++i) { while(!concurrent.Push(i)) { if(stop.stop_requested()) return; std::this_thread::yield(); } } });
        for(unsigned i=0;i<50000;++i) { unsigned value=0; while(!concurrent.Pop(value)) std::this_thread::yield(); Require(value==i,"Concurrent producer/consumer use must preserve publication order and values."); }
        producer.join();
        Require(ParseDisplayCaptureSeconds(L"1")==1u && ParseDisplayCaptureSeconds(L"60")==60u,"Bounded opt-in duration endpoints failed.");
        for(const auto invalid:{L"",L"0",L"61",L"-1",L"1.5",L"abc",L"1000"}) Require(!ParseDisplayCaptureSeconds(invalid),"Invalid opt-in duration must be rejected.");
        std::cout<<"PASS: consumed-reading aggregation, original reset timestamps, generation isolation, loss/overflow, bounded queues and opt-in argument bounds.\n"; return 0;
    }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
