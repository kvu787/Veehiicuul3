// Integration adapter: the application has no upstream GPU debug console.
#include <cstdint>
int PrintTime(std::uint64_t) { return 0; }
int PrintTimeDelta(std::uint64_t) { return 0; }
