#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace Ui
{
inline constexpr float Width=2560,Height=1440;
struct Rect
{
    float x=0,y=0,w=0,h=0;
    bool Contains(float px,float py) const { return px>=x && py>=y && px<x+w && py<y+h; }
};
inline Rect Intersect(Rect a,Rect b)
{
    const float x=std::max(a.x,b.x),y=std::max(a.y,b.y);
    return {x,y,std::max(0.f,std::min(a.x+a.w,b.x+b.w)-x),std::max(0.f,std::min(a.y+a.h,b.y+b.h)-y)};
}
enum class Kind { Label,Button,Number,Slider,KnotVector };
struct Control
{
    int id=0;
    Kind kind=Kind::Label;
    Rect rect,clip;
    std::wstring text;
    bool enabled=true,selected=false;
    double value=0,minimum=0,maximum=1;
};
enum class ActionKind { Click,Commit,BeginSlider,EndSlider,CancelSlider,CommitKnots };
struct Action { ActionKind kind; int id; double value=0; std::wstring text; };
class State
{
public:
    std::vector<Control> controls;
    Rect panel{0,62,438,1316};
    float scroll=0,contentHeight=0;
    int hover=0,focus=0,capture=0;
    std::wstring edit;
    size_t caret=0,anchor=0;
    bool invalid=false;
    void Begin();
    void Add(Control control,bool scrolling=true);
    void Finish(float height);
    const Control* Find(int id) const;
    int Hit(float x,float y) const;
    Rect Thumb() const;
    bool Move(float x,float y);
    std::vector<Action> Down(float x,float y);
    std::vector<Action> Up(float x,float y);
    std::vector<Action> Commit();
    std::vector<Action> Cancel();
    bool SetScroll(float position);
    bool Wheel(float delta);
    void Text(wchar_t value);
    void Replace(std::wstring value);
    void Erase(bool backward);
    void Left(bool right,bool selecting);
    void Home(bool end,bool selecting);
    void SelectAll();
    std::wstring Selected() const;
    bool Editing() const;
    size_t EditLimit() const;
private:
    float scrollAnchor_=0,pointerAnchor_=0;
    void Reveal(const Control& c);
    void RemoveSelection();
};
// Dirty state alone cannot cause frames while minimized or occluded. Active
// work is a separately armed timer; idle static editing has no timer.
struct Visibility
{
    bool shown=false,sessionAvailable=true,desktopAvailable=true,displayOn=true,cloaked=false,suspended=false;
    bool Blocked() const { return !shown || !sessionAvailable || !desktopAvailable || !displayOn || cloaked || suspended; }
};
struct Schedule
{
    bool dirty=true,minimized=false,occluded=false,active=false;
    bool WantsFrame() const { return dirty && !minimized && !occluded; }
    bool WantsTimer() const { return active && !minimized && !occluded; }
};
}
