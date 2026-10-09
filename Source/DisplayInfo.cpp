#include "Display.h"
#include <iostream>
#include <iomanip>
int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try
    {
        const auto displays=EnumerateDisplays();
        std::wcout << L"Normal launch selects Windows main display: " << displays[PrimaryDisplay(displays)].Label() << L"\n\n";
        for(const auto& display : displays)
            std::wcout << display.Label() << L"\nIdentity: " << display.identity << L"\nBounds: " << display.bounds.left << L',' << display.bounds.top << L',' << display.bounds.right << L',' << display.bounds.bottom
                << L"\nWork area: " << display.work.left << L',' << display.work.top << L',' << display.work.right << L',' << display.work.bottom << L"\nWindows primary: " << display.primary << L"\n\n";
        return 0;
    }
    catch(const std::exception& error) { std::cerr << error.what(); return 1; }
}
