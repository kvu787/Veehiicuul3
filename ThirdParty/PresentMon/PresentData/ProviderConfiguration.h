#pragma once
#include <windows.h>
#include <evntrace.h>
#include <memory>
#include "IFilterBuildListener.h"
struct PMTraceConsumer;
ULONG EnableProvidersListing(TRACEHANDLE sessionHandle, const GUID* sessionGuid,
    const PMTraceConsumer* consumer, bool filterEventIds, bool windows11,
    std::shared_ptr<IFilterBuildListener> listener = {});
