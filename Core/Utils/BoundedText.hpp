#pragma once
#include <string>
#include <string_view>

namespace SST {
inline std::string BoundedText(std::string_view text, std::size_t limit) {
    if(text.size()<=limit) return std::string(text);
    constexpr std::string_view prefix="[truncated] ";
    if(limit<=prefix.size()) return std::string(prefix.substr(0,limit));
    auto begin=text.size()-limit+prefix.size();
    while(begin<text.size() && (static_cast<unsigned char>(text[begin])&0xC0)==0x80) ++begin;
    return std::string(prefix)+std::string(text.substr(begin));
}
}
