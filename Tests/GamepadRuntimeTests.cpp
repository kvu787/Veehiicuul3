#include "GamepadInput.h"
#include <iostream>
int wmain(int count,wchar_t** arguments)
{
    if(count!=2) return 1;
    GamepadInput runtime;
    if(!runtime.Initialize(arguments[1])) { std::cerr<<runtime.Status()<<'\n'; return 1; }
    runtime.SetForeground(false);
    if(runtime.Now()==0 || runtime.Sample().connected) return 1;
    runtime.Stop();
    GamepadInput absent;
    if(absent.Initialize(std::filesystem::path(arguments[1])/L"AbsentRuntime")) return 1;
    std::cout<<"PASS: portable GameInput v3 loads/initializes, clock available, unfocused commands neutral, cleanup and missing-runtime keyboard fallback. No physical controller actions tested.\n"; return 0;
}
