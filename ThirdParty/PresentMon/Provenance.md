# PresentMon display decoder

Source: the local PresentMon v2.6.0 checkout, commit `e13fce6acdb55a808fd8318175a56863e532d95f`.
Upstream: https://github.com/GameTechDev/PresentMon/tree/v2.6.0
License: MIT, included in `LICENSE.txt`; source copyright notices are retained.

The application compiles the PresentData decoder directly into its executable.
It does not start an external PresentMon executable or depend on its service.

Integration changes:

- Source line endings and trailing whitespace are normalized for repository review.

- `ProviderConfiguration.cpp` extracts provider configuration from `PresentMonTraceSession.cpp`.
- `ProviderConfiguration.h` declares that extracted function.
- `Hash.cpp` includes the Windows SDK directly instead of the upstream wrapper.
- `Debug.hpp` and `TraceLogging.h/.cpp` use a diagnostic counter and standard exceptions instead of the service logging/exception libraries. Decoder warnings remain observable.
- `PresentMonWarnings.h` implements that counter.
- `ConsoleFormatting.cpp` disables unused GPU debug console formatting.
- The upstream session and verbose debug source are retained for provenance but are not compiled.
- The decoder uses its production `NDEBUG` behavior in both application configurations, so a modal upstream CRT assertion cannot block ETW shutdown. Upstream diagnostic counters remain enabled. The application's own Debug configuration still enables DX12 debug validation.

All presentation classification, composition correlation, and display-event decoding logic is retained from upstream.
