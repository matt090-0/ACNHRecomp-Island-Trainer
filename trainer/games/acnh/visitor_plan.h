#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// NHSE GSaveVisitorNpc, MainSaveOffsets30 and Villager2 establish these 3.x layouts.
// Sunday is day zero. Schedule editing takes effect when the game recreates visitors;
// it does not create a live actor or bypass the game's story, time and weather gates.
namespace nx::visitors {
enum class Visitor : int32_t {
    None=0, Gulliver=1, Label=2, Saharah=3, Wisp=4, Mabel=5, CJ=6,
    Flick=7, Kicks=8, Leif=9, Redd=10, Gullivarrr=11, Celeste=100
};
using Record=std::array<uint8_t,0x78>;
constexpr uint64_t kVisitorFromLandIdentity=0x494500;
constexpr uint64_t kWeatherFromLandIdentity=0x1e35dc;
constexpr uint64_t kFruitFromLandIdentity=0x49504c;
template<class T> inline T field(const void* data,size_t offset){T value{};std::memcpy(&value,static_cast<const uint8_t*>(data)+offset,sizeof value);return value;}
inline bool valid_record(const Record& data){
    for(size_t i=0;i<7;++i){if(field<int32_t>(data.data(),i*4)<0||field<int32_t>(data.data(),i*4)>11)return false;
        if(field<int32_t>(data.data(),0x44+i*4)<0||field<int32_t>(data.data(),0x44+i*4)>11)return false;}
    for(size_t i=0x1c;i<0x34;i+=4){const float f=field<float>(data.data(),i);if(!std::isfinite(f)||std::abs(f)>100000.f)return false;}
    for(size_t i=0x34;i<0x42;++i)if(data[i]>1)return false;
    for(size_t i=0x60;i<0x6e;++i)if(data[i]>1)return false;
    for(size_t i: {size_t(0x70),size_t(0x74)})if(field<int32_t>(data.data(),i)<-1||field<int32_t>(data.data(),i)>6)return false;
    return true;
}
inline bool fruit(uint16_t id){return id==2213||id==2214||id==2285||id==2286||id==2287;}
struct Patch {size_t offset;int32_t before,after;};
inline bool plan(const Record& data,Visitor visitor,int day,std::vector<Patch>& changes,std::string& message){
    changes.clear();
    if(!valid_record(data)){message="Visitor data changed. Find visitors again.";return false;}
    const bool special=visitor==Visitor::Celeste||visitor==Visitor::Wisp;
    if(day< (special?-1:0)||day>6){message="Choose a day from Sunday to Saturday.";return false;}
    const int32_t id=static_cast<int32_t>(visitor);
    if(!special&&(id<0||id>11)){message="Unknown visitor. Nothing changed.";return false;}
    auto add=[&](size_t offset,int32_t after){const auto before=field<int32_t>(data.data(),offset);if(before!=after)changes.push_back({offset,before,after});};
    if(special)add(visitor==Visitor::Celeste?0x74:0x70,day);
    else{add(size_t(day)*4,id);add(0x44+size_t(day)*4,id);}
    return true;
}
} // namespace nx::visitors
