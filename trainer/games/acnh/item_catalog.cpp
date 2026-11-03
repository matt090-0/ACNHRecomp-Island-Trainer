#include "item_catalog.h"
#include "platform.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <map>
#include <stdexcept>

namespace nx::items {
namespace {
struct Catalog {
    std::vector<Entry> entries;
    std::map<uint16_t,size_t> index;
    std::string error;
    Catalog() {
        try {
            std::ifstream in(std::filesystem::u8path(plat::data_root()+"/mod-menu-items.tsv"),std::ios::binary);
            if(!in) throw std::runtime_error("Item catalog missing. Run the local catalog generator for this game folder.");
            std::string line;
            if(!std::getline(in,line)) throw std::runtime_error("Item catalog is empty.");
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line!="ACNH_MENU_ITEMS_V1") throw std::runtime_error("Unsupported item catalog. Regenerate it from your game folder.");
            while(std::getline(in,line)) {
                if(!line.empty() && line.back()=='\r') line.pop_back();
                if(line.empty()) continue;
                std::vector<std::string> fields;
                size_t pos=0;
                for(;;) {
                    const auto end=line.find('\t',pos);
                    fields.push_back(line.substr(pos,end-pos));
                    if(end==std::string::npos) break;
                    pos=end+1;
                }
                auto number=[&](size_t i) {
                    if(i>=fields.size() || fields[i].empty() || fields[i].find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("Invalid item catalog number.");
                    const auto value=std::stoull(fields[i]);
                    if(value>UINT32_MAX) throw std::runtime_error("Oversized item catalog number.");
                    return uint32_t(value);
                };
                if(fields[0]=="I" && fields.size()==6) {
                    const auto id=number(1),stack=number(2),tool=number(3);
                    if(!id || id>0x7fff || !stack || stack>999 || tool>1 || fields[4].empty() || fields[5].empty() || entries.size()>=30000 || index.count(uint16_t(id))) throw std::runtime_error("Invalid item catalog record.");
                    index[uint16_t(id)]=entries.size();
                    entries.push_back(Entry{uint16_t(id),fields[5],fields[4],uint16_t(stack),tool!=0,{}});
                } else if(fields[0]=="V" && fields.size()==3 && !entries.empty()) {
                    const auto param=number(1);
                    if(fields[2].empty() || entries.back().variants.size()>=4096) throw std::runtime_error("Invalid item variant.");
                    entries.back().variants.push_back(Variant{fields[2],param});
                } else throw std::runtime_error("Invalid item catalog line.");
            }
            if(entries.empty()) throw std::runtime_error("Item catalog is empty.");
            for(const auto& entry:entries) if(entry.variants.empty()) throw std::runtime_error("Item catalog is incomplete. Regenerate it from your game folder.");
        } catch(const std::exception& e) { entries.clear();index.clear();error=e.what(); }
    }
};
const Catalog& data() { static const Catalog value;return value; }
struct ToolChain { uint16_t flimsy, intermediate, normal, golden; std::array<uint16_t,6> others; };
constexpr std::array<ToolChain,6> tools{{
    {3068,5856,2100,9617,{775,12615,0,0,0,0}},
    {3084,0,2377,8660,{8471,8472,8473,776,0,0}},
    {2376,0,5784,8179,{8176,8177,8178,779,0,0}},
    {3082,0,2099,8574,{8575,8576,8577,773,0,0}},
    {3083,0,2379,8533,{8534,8535,8536,0,0,0}},
    {0,0,2822,8578,{8579,8580,0,0,0,0}}
}};
const ToolChain* chain(uint16_t id) {
    if(!id) return nullptr;
    for(const auto& t:tools) if(id==t.flimsy || id==t.intermediate || id==t.normal || id==t.golden || std::find(t.others.begin(),t.others.end(),id)!=t.others.end()) return &t;
    return nullptr;
}
}
const std::vector<Entry>& catalog() { return data().entries; }
const std::string& catalog_error() { return data().error; }
const Entry* find(uint16_t id) {
    const auto& d=data();const auto it=d.index.find(id);
    return it==d.index.end() ? nullptr : &d.entries[it->second];
}
bool make_item(uint16_t id,unsigned count,unsigned variant,uint64_t& raw,std::string& error) {
    const auto* entry=find(id);
    if(!entry) {error="This item is not in the local player-item catalog.";return false;}
    if(!count || count>entry->max_stack) {error="Choose a quantity from 1 to "+std::to_string(entry->max_stack)+".";return false;}
    if(variant>=entry->variants.size()) {error="Choose a valid item variation.";return false;}
    uint32_t param=entry->variants[variant].free_param;
    if(entry->max_stack>1) {
        if(param&0xffff) {error="Stack metadata conflicts with this item variation.";return false;}
        param=(param&0xffff0000u)|(count-1);
    }
    raw=uint64_t(id)|(uint64_t(param)<<32);
    error.clear();return true;
}
uint64_t upgrade_tool(uint64_t raw,bool golden) {
    const uint16_t id=uint16_t(raw);const auto* t=chain(id);
    if(!t || id==t->golden) return raw;
    uint16_t target=golden ? t->golden : id==t->flimsy ? (t->intermediate ? t->intermediate : t->normal) : id==t->intermediate ? t->normal : t->golden;
    // Preserve wrapping/system flags; a new tool starts with zero uses and its default appearance.
    return (raw&0xffff0000ull)|target;
}
uint64_t repair_tool(uint64_t raw) {
    return chain(uint16_t(raw)) ? raw&0x0000ffffffffffffull : raw;
}
} // namespace nx::items
