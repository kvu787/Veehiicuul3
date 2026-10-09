#include "Ui.h"
#include <cwchar>
#include <iomanip>
#include <sstream>

namespace Ui
{
void State::Begin() { controls.clear(); }
void State::Add(Control c,bool scrolling)
{
    if(scrolling) { c.rect.y+=panel.y-scroll; c.clip=panel; }
    controls.push_back(std::move(c));
}
void State::Finish(float height)
{
    contentHeight=height;
    SetScroll(scroll);
    if(focus && (!Find(focus) || !Find(focus)->enabled)) { focus=0; edit.clear(); }
}
bool State::SetScroll(float position)
{
    const auto next=std::clamp(position,0.f,std::max(0.f,contentHeight-panel.h));
    const float difference=scroll-next;
    if(!difference) return false;
    // Keep drawing and pointer routing in the same coordinates, including when
    // more wheel/capture messages arrive before the next on-demand frame.
    for(auto& c:controls)
        if(c.clip.x==panel.x && c.clip.y==panel.y && c.clip.w==panel.w && c.clip.h==panel.h) c.rect.y+=difference;
    scroll=next; hover=0;
    return true;
}
const Control* State::Find(int id) const { for(auto c=controls.rbegin();c!=controls.rend();++c) if(c->id==id) return &*c; return nullptr; }
int State::Hit(float x,float y) const
{
    if(contentHeight>panel.h && Rect{panel.x+panel.w-14,panel.y,14,panel.h}.Contains(x,y)) return -1;
    for(auto i=controls.rbegin();i!=controls.rend();++i)
        if(i->id && i->enabled && i->kind!=Kind::Label && Intersect(i->rect,i->clip).Contains(x,y)) return i->id;
    return 0;
}
Rect State::Thumb() const
{
    const auto height=std::max(32.f,panel.h*panel.h/std::max(panel.h,contentHeight));
    const auto travel=panel.h-height,range=std::max(1.f,contentHeight-panel.h);
    return {panel.x+panel.w-12,panel.y+travel*scroll/range,10,height};
}
bool State::Move(float x,float y)
{
    if(capture==-1)
    {
        const auto thumb=Thumb();
        return SetScroll(scrollAnchor_+(y-pointerAnchor_)*(contentHeight-panel.h)/std::max(1.f,panel.h-thumb.h));
    }
    const auto old=hover; hover=Hit(x,y); return old!=hover;
}
void State::Reveal(const Control& c)
{
    if(c.clip.y!=panel.y) return;
    auto next=scroll;
    if(c.rect.y<panel.y) next-=panel.y-c.rect.y;
    else if(c.rect.y+c.rect.h>panel.y+panel.h) next+=c.rect.y+c.rect.h-panel.y-panel.h;
    SetScroll(next);
}
std::vector<Action> State::Down(float x,float y)
{
    auto actions=Commit(); const auto id=Hit(x,y);
    if(id==-1)
    {
        capture=-1;
        const auto thumb=Thumb();
        if(!thumb.Contains(x,y)) SetScroll(scroll+(y<thumb.y ? -panel.h*.8f : panel.h*.8f));
        pointerAnchor_=y; scrollAnchor_=scroll; return actions;
    }
    const auto* c=Find(id); if(!c) { focus=0; return actions; }
    focus=id; invalid=false;
    if(c->kind==Kind::Number || c->kind==Kind::KnotVector)
    {
        if(c->kind==Kind::KnotVector) edit=c->text;
        else { std::wostringstream s; s<<std::setprecision(17)<<c->value; edit=s.str(); }
        SelectAll(); Reveal(*c);
    }
    else if(c->kind==Kind::Slider) { capture=id; actions.push_back({ActionKind::BeginSlider,id,c->value}); }
    else capture=id;
    return actions;
}
std::vector<Action> State::Up(float x,float y)
{
    const auto id=capture; capture=0;
    if(id<=0) return {};
    const auto* c=Find(id); if(!c) return {};
    if(c->kind==Kind::Slider) return {{ActionKind::EndSlider,id}};
    if(Hit(x,y)==id) return {{ActionKind::Click,id}};
    return {};
}
bool State::Editing() const { const auto* c=Find(focus); return c && (c->kind==Kind::Number || c->kind==Kind::KnotVector) && c->enabled; }
size_t State::EditLimit() const { const auto* c=Find(focus); return c && c->kind==Kind::KnotVector ? 2048 : 128; }
std::vector<Action> State::Commit()
{
    if(!Editing()) return {};
    const auto id=focus; const auto* c=Find(id);
    // The application validates the whole periodic vector before clearing this
    // draft. Invalid input remains editable; clicking elsewhere is intercepted.
    if(c->kind==Kind::KnotVector) return {{ActionKind::CommitKnots,id,0,edit}};
    wchar_t* end=nullptr; const auto value=std::wcstod(edit.c_str(),&end);
    if(edit.empty() || !end || *end || !std::isfinite(value) || value<c->minimum || value>c->maximum) { invalid=true; return {}; }
    focus=0; edit.clear(); invalid=false;
    return {{ActionKind::Commit,id,value}};
}
std::vector<Action> State::Cancel()
{
    const auto id=capture; capture=focus=0; edit.clear(); invalid=false;
    const auto* c=Find(id);
    if(c && c->kind==Kind::Slider) return {{ActionKind::CancelSlider,id}};
    return {};
}
bool State::Wheel(float delta)
{
    return SetScroll(scroll-delta*.8f);
}
void State::RemoveSelection()
{
    const auto first=std::min(caret,anchor),last=std::max(caret,anchor);
    edit.erase(first,last-first); caret=anchor=first;
}
void State::Replace(std::wstring value)
{
    if(!Editing() || value.size()+edit.size()-(std::max(caret,anchor)-std::min(caret,anchor))>EditLimit()) return;
    const bool knots=Find(focus)->kind==Kind::KnotVector;
    for(auto& c:value)
    {
        if(knots && (c==L'\r' || c==L'\n' || c==L'\t')) c=L' ';
        if(!((c>=L'0' && c<=L'9') || c==L'.' || c==L'-' || c==L'+' || c==L'e' || c==L'E' || (knots && (c==L' ' || c==L',')))) return;
    }
    RemoveSelection(); edit.insert(caret,value); caret+=value.size(); anchor=caret; invalid=false;
}
void State::Text(wchar_t value) { if(value>=L' ') Replace(std::wstring(1,value)); }
void State::Erase(bool backward)
{
    if(!Editing()) return;
    if(caret!=anchor) RemoveSelection();
    else if(backward && caret) { edit.erase(--caret,1); anchor=caret; }
    else if(!backward && caret<edit.size()) edit.erase(caret,1);
    invalid=false;
}
void State::Left(bool right,bool selecting)
{
    if(!selecting && caret!=anchor) caret=right ? std::max(caret,anchor) : std::min(caret,anchor);
    else caret=right ? std::min(edit.size(),caret+1) : (caret ? caret-1 : 0);
    if(!selecting) anchor=caret;
}
void State::Home(bool end,bool selecting) { caret=end ? edit.size() : 0; if(!selecting) anchor=caret; }
void State::SelectAll() { anchor=0; caret=edit.size(); }
std::wstring State::Selected() const { return edit.substr(std::min(caret,anchor),std::max(caret,anchor)-std::min(caret,anchor)); }
}
