#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Presentation.h"

struct Display
{
    std::wstring device, name, operatingSystemName, identity;
    RECT bounds{}, work{};
    bool primary = false;
    std::wstring Label() const { return name+L" | "+device+L" | "+operatingSystemName; }
};
std::vector<Display> EnumerateDisplays();
size_t ResolveDisplay(const std::vector<Display>& displays,const std::wstring& name);
size_t PrimaryDisplay(const std::vector<Display>& displays);
Presentation DisplayPresentation(const Display& display);
void ValidateDisplay(const Display& display,const Presentation& presentation);
bool InputDesktopAvailable();
