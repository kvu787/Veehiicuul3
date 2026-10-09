#pragma once
#include <optional>
#include <string_view>
inline std::optional<unsigned> ParseDisplayCaptureSeconds(std::wstring_view text) noexcept
{
    if(text.empty() || text.size()>2) return std::nullopt;
    unsigned seconds=0;
    for(const auto digit:text) { if(digit<L'0' || digit>L'9') return std::nullopt; seconds=seconds*10+static_cast<unsigned>(digit-L'0'); }
    return seconds>=1 && seconds<=60 ? std::optional<unsigned>{seconds} : std::nullopt;
}
