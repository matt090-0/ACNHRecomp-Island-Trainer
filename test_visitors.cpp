#include "runtime-source/games/acnh/visitor_menu.h"
#include <windows.h>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {std::string root;}
namespace nx::plat {std::string data_root(){return root;}}
namespace nx::plat {FILE* fopen_utf8(const std::string& p,const char*){return _wfopen(std::filesystem::u8path(p).c_str(),L"a");}}
namespace nx {void log(const char* text,...){va_list ap;va_start(ap,text);vprintf(text,ap);putchar('\n');va_end(ap);}}
using namespace nx::visitors;
template<class T> void put(void* data,size_t offset,T value){std::memcpy(static_cast<uint8_t*>(data)+offset,&value,sizeof value);}
int main(int argc,char** argv){
    assert(argc==2);root=argv[1];std::filesystem::create_directories(root);std::ofstream(root+"/game.ini")<<"build_id=FCD2BB238ABE99E925B4E452DF1F41F1\n";
    Record record{};put(record.data(),0x70,int32_t(-1));put(record.data(),0x74,int32_t(-1));assert(valid_record(record));
    std::vector<Patch> patches;std::string message;assert(plan(record,Visitor::Redd,2,patches,message));assert(patches.size()==2&&patches[0].offset==8&&patches[1].offset==0x4c&&patches[0].after==10);
    assert(plan(record,Visitor::Celeste,2,patches,message));assert(patches.size()==1&&patches[0].offset==0x74&&patches[0].after==2);
    assert(plan(record,Visitor::Wisp,4,patches,message));assert(patches.size()==1&&patches[0].offset==0x70&&patches[0].after==4);
    assert(plan(record,Visitor::Celeste,-1,patches,message)&&patches.empty());
    assert(!plan(record,Visitor::Redd,-1,patches,message));assert(!plan(record,Visitor(123),2,patches,message));assert(!plan(record,Visitor::Redd,7,patches,message));
    auto bad=record;put(bad.data(),0x44,int32_t(12));assert(!valid_record(bad));bad=record;bad[0x34]=2;assert(!valid_record(bad));bad=record;put(bad.data(),0x1c,float(INFINITY));assert(!valid_record(bad));bad=record;put(bad.data(),0x74,int32_t(7));assert(!valid_record(bad));
    constexpr uint64_t base=0x5012000000ULL;constexpr size_t stride=0x600000;void* heap=VirtualAlloc(reinterpret_cast<void*>(base),2*stride,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(heap==reinterpret_cast<void*>(base));
    std::string key(0x38,'\0');put(key.data(),0,uint32_t(0x12345678));key[4]='I';key[0x18]=2;put(key.data(),0x1c,uint32_t(0x76543210));key[0x20]='P';
    auto prepare=[&](uint64_t a){std::memcpy(reinterpret_cast<void*>(a),key.data(),0x1c);std::memcpy(reinterpret_cast<void*>(a+kVisitorFromLandIdentity),record.data(),record.size());uint8_t weather[0x20]{};put(weather,0,uint16_t(2026));weather[2]=11;weather[3]=3;weather[4]=5;std::memcpy(reinterpret_cast<void*>(a+kWeatherFromLandIdentity),weather,sizeof weather);put(reinterpret_cast<void*>(a+kFruitFromLandIdentity),0,uint16_t(2213));put(reinterpret_cast<void*>(a+kFruitFromLandIdentity),2,uint16_t(2286));};
    prepare(base);prepare(base+stride);scan(key);for(int i=0;state().scanning&&i<500;++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(state().copies==2);
    assert(request_schedule(key,Visitor::Redd,2,message));assert(state().pending);assert(field<int32_t>(reinterpret_cast<void*>(base+kVisitorFromLandIdentity),8)==0);process_pending();assert(!state().pending&&state().daily[2]==10);
    for(uint64_t a:{base,base+stride}){assert(field<int32_t>(reinterpret_cast<void*>(a+kVisitorFromLandIdentity),8)==10);assert(field<int32_t>(reinterpret_cast<void*>(a+kVisitorFromLandIdentity),0x4c)==10);assert(field<int32_t>(reinterpret_cast<void*>(a+kVisitorFromLandIdentity),4)==0);assert(field<int32_t>(reinterpret_cast<void*>(a+kVisitorFromLandIdentity),12)==0);}
    assert(request_schedule(key,Visitor::Celeste,2,message));process_pending();assert(state().celeste_day==2&&state().daily[2]==10);
    assert(request_schedule(key,Visitor::Celeste,-1,message));process_pending();assert(state().celeste_day==-1&&state().daily[2]==10);
    assert(request_schedule(key,Visitor::Wisp,3,message));process_pending();assert(state().wisp_day==3&&state().celeste_day==-1);
    *reinterpret_cast<uint8_t*>(base)=0;assert(request_schedule(key,Visitor::Redd,1,message));process_pending();assert(field<int32_t>(reinterpret_cast<void*>(base+stride+kVisitorFromLandIdentity),4)==0);assert(state().message.find("changed")!=std::string::npos);
    VirtualFree(heap,0,MEM_RELEASE);std::cout<<"Visitor layout, schedule planning, deferred writes, exact field preservation, special visitors and stale identity tests passed.\n";
}
