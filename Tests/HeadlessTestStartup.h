#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "../Source/HeadlessRuntime.h"

// Forced into owned console test translation units and nested copy checks.
// Inline initialization installs one process-local policy before main/wmain.
inline const bool HeadlessTestStartup=[]
{
    HeadlessRuntime::Configure();
    if(!HeadlessRuntime::Configured()) HeadlessRuntime::Fail("startup policy configuration failed");
    HeadlessRuntime::Probe(HeadlessRuntime::ProbeArgument(GetCommandLineW()));
    return true;
}();
