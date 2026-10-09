#include "Ui.h"
#include "Presentation.h"
#include <iostream>
#include <stdexcept>
void Check(bool c,const char* text) { if(!c) throw std::runtime_error(text); }
int main()
{
    try
    {
        Presentation p{3840,2400,5120,-958,96,96}; Check(p.Supported(),"Valid display rejected"); Check(p.Image().x==640 && p.Image().y==480,"Center offsets wrong");
        Check(!p.ClientPoint(639,480) && !p.ClientPoint(3200,480),"Black margins active"); const auto point=p.ScreenPoint(5760,-478); Check(point && (*point)[0]==0 && (*point)[1]==0,"Negative-origin pointer mapping wrong");
        p={2560,1440,-2560,-1440,96,96}; Check(p.Supported() && p.Image().x==0,"Exact threshold rejected"); p.width=2559; Check(!p.Supported(),"Too-small width accepted"); p.width=2560;p.height=1439;Check(!p.Supported(),"Too-small height accepted");p.height=1440;p.dpiX=120;Check(!p.Supported(),"125% scaling accepted");p.dpiX=96;p.dpiY=120;Check(!p.Supported(),"Unequal DPI accepted");
        Ui::State ui; ui.Begin(); Ui::Control number;number.id=1;number.kind=Ui::Kind::Number;number.rect={12,10,200,38};number.value=.123456789012345;number.minimum=-100;number.maximum=100;ui.Add(number); Ui::Control disabled=number;disabled.id=2;disabled.rect.y=60;disabled.enabled=false;ui.Add(disabled);ui.Finish(2500);
        Check(ui.Hit(20,75)==1 && ui.Hit(20,125)==0,"Disabled or clipped hit routing wrong"); ui.Down(20,75); Check(ui.Editing() && ui.caret==ui.edit.size(),"Numeric selection failed");ui.Replace(L"-1.25e1");auto actions=ui.Commit();Check(actions.size()==1 && actions[0].value==-12.5,"Scientific numeric commit wrong");
        ui.Down(20,75);ui.Replace(L"1e");Check(ui.Commit().empty() && ui.invalid,"Malformed numeric accepted");ui.Cancel();ui.Down(20,75);ui.Replace(L"inf");Check(ui.edit!=L"inf","Non-numeric paste accepted");ui.Replace(L"99");ui.Left(false,false);ui.Erase(true);Check(ui.edit==L"9","Caret/delete failed");ui.SelectAll();Check(ui.Selected()==L"9","Selection copy failed");ui.Cancel();
        Check(ui.Wheel(-120) && ui.scroll>0,"Wheel scrolling failed");ui.Begin();ui.Add(number);ui.Finish(2500);Check(ui.Hit(20,75)==0,"Offscreen field hittable");const auto before=ui.scroll;const auto thumb=ui.Thumb();ui.Down(thumb.x+2,thumb.y+2);ui.Move(thumb.x+2,thumb.y+102);Check(ui.scroll>before && ui.Thumb().y>thumb.y,"Scrollbar not synchronized");ui.Up(thumb.x,thumb.y);ui.Begin();ui.Add(number);ui.Finish(100);Check(ui.scroll==0,"Scroll not clamped on content shrink");
        Ui::Schedule schedule;Check(schedule.WantsFrame() && !schedule.WantsTimer(),"Idle schedule wrong");schedule.dirty=false;Check(!schedule.WantsFrame(),"Clean editor rendering");schedule.active=true;schedule.minimized=true;Check(!schedule.WantsTimer(),"Minimized driving timer active");schedule.minimized=false;schedule.occluded=true;Check(!schedule.WantsTimer(),"Occluded driving timer active");
        Ui::Visibility visibility;Check(visibility.Blocked(),"Hidden startup considered visible");visibility.shown=true;Check(!visibility.Blocked(),"Visible state blocked");
        for(bool* signal:{&visibility.sessionAvailable,&visibility.desktopAvailable,&visibility.displayOn}) { *signal=false;Check(visibility.Blocked(),"Unavailable platform signal ignored");*signal=true; }
        visibility.cloaked=true;Check(visibility.Blocked(),"Cloaking ignored");visibility.cloaked=false;visibility.suspended=true;Check(visibility.Blocked(),"Suspension ignored");
        Ui::State knots; Ui::Control vector;vector.id=4;vector.kind=Ui::Kind::KnotVector;vector.rect={12,0,404,38};vector.text=L"0 1 2 3 4";knots.Begin();knots.Add(vector);knots.Finish(100);
        knots.Down(20,75);knots.Replace(L"-1,+0\n0\t1 1 2e0");Check(knots.edit==L"-1,+0 0 1 1 2e0","Knot paste/selection normalization failed");
        const auto draft=knots.edit;auto commit=knots.Commit();Check(commit.size()==1 && commit[0].kind==Ui::ActionKind::CommitKnots && commit[0].text==draft && knots.Editing(),"Vector cleared before atomic application validation");
        knots.Home(false,false);knots.Left(true,true);knots.Replace(L"0");Check(knots.edit.front()==L'0',"Knot caret/selection editing failed");knots.Home(true,false);knots.Text(L'\r');Check(knots.caret==knots.edit.size(),"Enter character mutated vector text");
        knots.SelectAll();knots.Replace(std::wstring(2048,L'0'));Check(knots.edit.size()==2048,"Full bounded vector replacement rejected due to prior selected text length");const auto full=knots.edit;knots.Text(L'1');Check(knots.edit==full,"Knot input exceeded buffer bound");knots.SelectAll();knots.Replace(std::wstring(2049,L'0'));Check(knots.edit==full,"Oversized paste partially replaced knot text");knots.Cancel();Check(!knots.Editing() && knots.edit.empty(),"Knot cancel retained draft/focus");
        Ui::State routed; routed.panel={0,62,438,100};
        Ui::Control button;button.id=8;button.kind=Ui::Kind::Button;button.rect={12,96,404,36};
        Ui::Control fixed=button;fixed.id=9;fixed.rect={12,12,100,38};fixed.clip={0,0,2560,1440};
        routed.Begin();routed.Add(button);routed.Add(fixed,false);routed.Finish(600);
        Check(Ui::Intersect(routed.Find(8)->rect,routed.panel).h==4 && routed.Hit(20,159)==8 && routed.Hit(20,162)==0,"Partial bottom control hit escaped its clip");
        routed.Down(20,159);Check(routed.capture==8,"Partial control failed to capture");
        Check(routed.Wheel(-120) && routed.Find(8)->rect.y==62 && routed.Find(9)->rect.y==12,"Wheel did not immediately synchronize panel coordinates or moved a fixed control");
        Check(routed.Up(20,70).size()==1 && routed.Hit(20,110)==0,"Wheel-before-release used stale pointer coordinates");
        routed.hover=8;Check(routed.SetScroll(130) && routed.hover==0 && Ui::Intersect(routed.Find(8)->rect,routed.panel).h==2 && routed.Hit(20,63)==8 && routed.Hit(20,61)==0,"Partial restored control retained stale hover or escaped the top clip");
        routed.SetScroll(96);const auto firstThumb=routed.Thumb();routed.Down(firstThumb.x+2,firstThumb.y+2);routed.Move(firstThumb.x+2,firstThumb.y+20);
        Check(routed.scroll>96 && routed.Find(8)->rect.y==158-routed.scroll,"Captured scrollbar failed immediate layout synchronization");routed.Cancel();
        routed.Begin();routed.Add(button);routed.Add(fixed,false);routed.Finish(120);
        Check(routed.scroll==20 && routed.Find(8)->rect.y==138 && routed.Hit(20,150)==8,"Shrink clamp did not synchronize restored drawing/hit regions");
        routed.Finish(600);Check(routed.scroll==20,"Content growth resurrected an unclamped position");
        Ui::State reveal;reveal.panel={0,62,438,100};number.rect.y=96;reveal.Begin();reveal.Add(number);reveal.Finish(600);reveal.Down(180,159);
        Check(reveal.scroll==34 && reveal.Find(1)->rect.y==124 && reveal.Hit(180,159)==1 && reveal.Editing(),"Partial numeric reveal waited for a frame or lost editing focus");
        fixed.id=8;routed.Add(fixed,false);Check(routed.Find(8)->rect.y==12 && routed.Hit(20,20)==8,"Popup lookup disagreed with topmost hit routing for a shared command ID");
        std::cout<<"Custom UI numeric, immediate clipping/hits, wheel/capture/shrink/reveal, centered mapping, startup policy and scheduling passed.\n"; return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
