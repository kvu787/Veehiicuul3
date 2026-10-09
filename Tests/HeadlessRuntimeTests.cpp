#include "HeadlessTestStartup.h"
#include <iostream>
int main()
{
    if(!HeadlessRuntime::Configured()) return 1;
    std::cout<<"Process-local headless reporting policy installed.\n";
    return 0;
}
