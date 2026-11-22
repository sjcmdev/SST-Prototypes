#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace SST::ScapsLab {
// Validate the ImGui file payload without applying it to the live context.
inline bool ValidDockLayout(std::string_view ini) {
    if(ini.empty())return true;
    constexpr std::uint32_t mainDockSpace=0x11111111;
    std::map<std::uint32_t,std::uint32_t> parents;
    std::vector<std::uint32_t> docks;
    bool appWindow=false,docking=false,mainFound=false;
    const auto hex=[](std::string_view line,std::string_view key,std::uint32_t &value) {
        const auto pos=line.find(key);if(pos==std::string_view::npos)return false;
        const auto *first=line.data()+pos+key.size(),*last=line.data()+line.size();
        const auto result=std::from_chars(first,last,value,16);
        return result.ec==std::errc{} && (result.ptr==last || *result.ptr==' ' || *result.ptr==',');
    };
    while(!ini.empty()) {
        const auto end=ini.find('\n');auto line=ini.substr(0,end);
        ini=end==std::string_view::npos?std::string_view{}:ini.substr(end+1);
        if(!line.empty() && line.back()=='\r')line.remove_suffix(1);
        if(line.starts_with("[")){appWindow=line.starts_with("[Window][sst-window-");docking=line=="[Docking][Data]";continue;}
        if(appWindow && line.starts_with("DockId=")){std::uint32_t id=0;if(!hex(line,"DockId=0x",id))return false;if(id)docks.push_back(id);}
        if(!docking)continue;
        const auto first=line.find_first_not_of(' ');if(first==std::string_view::npos)continue;line.remove_prefix(first);
        if(!line.starts_with("DockNode ") && !line.starts_with("DockSpace "))continue;
        std::uint32_t id=0,parent=0;
        if(!hex(line,"ID=0x",id) || !id)return false;
        if(line.find("Parent=")!=std::string_view::npos && !hex(line,"Parent=0x",parent))return false;
        if(!parents.emplace(id,parent).second)return false;
        if(id==mainDockSpace)mainFound=line.starts_with("DockSpace ") && parent==0;
    }
    if(!mainFound)return false;
    for(auto id:docks) {
        std::size_t depth=0;
        while(id!=mainDockSpace) {
            const auto it=parents.find(id);
            if(it==parents.end() || !it->second || ++depth>parents.size())return false;
            id=it->second;
        }
    }
    return true;
}
}
