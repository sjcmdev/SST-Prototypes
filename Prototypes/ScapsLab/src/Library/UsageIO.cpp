#include "Library/Library.hpp"
#include "Io.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>

namespace SST::ScapsLab::Library {
namespace {
using Json=nlohmann::json;
// SAX skips saved simulation arrays; where-used never materializes results or library tables.
class UsageReader final:public nlohmann::json_sax<Json> {
    struct Frame {bool device=false,document=false,uses=false,use=false;std::string key;Assets::Use value;};
    std::vector<Frame> stack;
    std::string deviceId,deviceName,project;
    std::vector<Assets::Use> deviceUses;
    bool version(double value) {
        if(!stack.empty() && stack.back().use && stack.back().key=="version"){
            if(!std::isfinite(value) || value<1 || value>UINT32_MAX || value!=std::floor(value)){diagnostic="Invalid asset version in project";return false;}
            stack.back().value.source.version=std::uint32_t(value);
        }return true;
    }
public:
    std::vector<Assets::Usage> usages;
    std::string diagnostic;
    explicit UsageReader(std::string path):project(std::move(path)){}
    bool null() override{return true;}
    bool boolean(bool) override{return true;}
    bool number_integer(number_integer_t v) override{return version(double(v));}
    bool number_unsigned(number_unsigned_t v) override{return version(double(v));}
    bool number_float(number_float_t v,const string_t&) override{return version(v);}
    bool string(string_t& text) override {
        if(stack.empty())return true;auto& f=stack.back();
        if(f.device){if(f.key=="id")deviceId=text;else if(f.key=="name")deviceName=text;}
        if(f.use){if(f.key=="library_id")f.value.source.libraryId=text;else if(f.key=="item_id")f.value.source.itemId=text;else if(f.key=="target")f.value.target=text;else if(f.key=="policy"){if(text!="copy" && text!="linked"){diagnostic="Unknown asset policy in project";return false;}f.value.policy=text=="linked"?Assets::UsePolicy::Linked:Assets::UsePolicy::Copy;}}
        return true;
    }
    bool binary(binary_t&) override{return true;}
    bool start_object(std::size_t) override {
        if(stack.size()>256){diagnostic="Project nesting is too deep";return false;}Frame f;
        if(!stack.empty()){const auto& parent=stack.back();f.device=stack.size()==2 && parent.key=="devices";f.document=parent.device && parent.key=="document";f.use=parent.uses;}
        if(f.device){deviceId.clear();deviceName.clear();deviceUses.clear();}stack.push_back(std::move(f));return true;
    }
    bool key(string_t& value) override {stack.back().key=value;return true;}
    bool end_object() override {
        const auto& f=stack.back();if(f.use){if(!Assets::ValidId(f.value.source.libraryId) || !Assets::ValidId(f.value.source.itemId) || !f.value.source.version || f.value.target.empty()){diagnostic="Invalid asset reference in project";return false;}deviceUses.push_back(f.value);}
        if(f.device){if(deviceId.empty()){diagnostic="Missing project device ID";return false;}for(auto& use:deviceUses)usages.push_back({project,deviceId,deviceName,std::move(use)});deviceUses.clear();}stack.pop_back();return true;
    }
    bool start_array(std::size_t) override {Frame f;if(!stack.empty()){const auto& parent=stack.back();f.key=parent.key;f.uses=parent.document && parent.key=="asset_uses";}stack.push_back(std::move(f));return true;}
    bool end_array() override {stack.pop_back();return true;}
    bool parse_error(std::size_t,const std::string&,const nlohmann::detail::exception& error) override {diagnostic=error.what();return false;}
};
}
std::vector<Assets::Usage> ScanProjects(const std::filesystem::path& folder) {
    Assets::Require(!folder.empty(),"Choose a project file or project folder");Io::Note("where-used-scan",folder.string());
    std::vector<std::filesystem::path> files;
    if(std::filesystem::is_regular_file(folder))files.push_back(folder);
    else {Assets::Require(std::filesystem::is_directory(folder),"Where-used folder does not exist");for(const auto& entry:std::filesystem::recursive_directory_iterator(folder,std::filesystem::directory_options::skip_permission_denied)){if(entry.is_regular_file() && Assets::Fold(entry.path().extension().string())==".scapslab")files.push_back(entry.path());Assets::Require(files.size()<=10000,"Where-used scan exceeds 10000 projects; choose a smaller folder");}}
    std::sort(files.begin(),files.end());std::vector<Assets::Usage> out;
    for(const auto& file:files){Io::Note("where-used-project",file.string());std::ifstream stream(file,std::ios::binary);if(!stream)throw Assets::Error(Assets::ErrorCode::Io,"Cannot read project: "+file.filename().string());UsageReader reader(std::filesystem::absolute(file).lexically_normal().string());if(!Json::sax_parse(stream,&reader))throw Assets::Error(Assets::ErrorCode::Invalid,"Cannot index "+file.filename().string()+": "+reader.diagnostic);out.insert(out.end(),std::make_move_iterator(reader.usages.begin()),std::make_move_iterator(reader.usages.end()));}return out;
}
}
