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
        std::cout<<"Custom UI numeric, clipping, scrolling, centered mapping, startup policy and scheduling passed.\n"; return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
