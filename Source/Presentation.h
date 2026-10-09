#pragma once
#include "Ui.h"
#include <array>
#include <stdexcept>

struct Presentation
{
    int width=0,height=0,originX=0,originY=0;
    unsigned dpiX=96,dpiY=96;
    bool Supported() const { return width>=2560 && height>=1440 && dpiX==96 && dpiY==96; }
    Ui::Rect Image() const { return {static_cast<float>((width-2560)/2),static_cast<float>((height-1440)/2),2560,1440}; }
    std::optional<std::array<float,2>> ClientPoint(float x,float y) const
    {
        const auto image=Image(); if(!Supported() || !image.Contains(x,y)) return std::nullopt;
        return std::array<float,2>{x-image.x,y-image.y};
    }
    std::optional<std::array<float,2>> ScreenPoint(float x,float y) const { return ClientPoint(x-static_cast<float>(originX),y-static_cast<float>(originY)); }
};
