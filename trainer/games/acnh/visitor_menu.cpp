#include "visitor_menu.h"
#include "platform.h"
#include <nx/runtime.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace nx::visitors {
namespace {
using Land=std::array<uint8_t,0x1c>;
struct Candidate {uint64_t anchor=0;Land land{};Record record{};};
struct Request {Land land{};Visitor visitor=Visitor::None;int day=0;};
struct Store {std::mutex lock;std::atomic<bool> scanning{false},signal{false};bool processing=false;std::vector<Candidate> candidates;std::optional<Request> request;Land selected{};std::string message="Find visitors after loading your island.";};
Store& store(){static Store* s=new Store;return *s;}
bool supported(){static const bool ok=[]{std::ifstream in(std::filesystem::u8path(plat::data_root()+"/game.ini"));std::string t((std::istreambuf_iterator<char>(in)),{});return t.find("FCD2BB238ABE99E925B4E452DF1F41F1")!=std::string::npos;}();return ok;}
bool land_from_key(const std::string& key,Land& land){if(key.size()!=0x38)return false;std::memcpy(land.data(),key.data(),land.size());const auto id=field<uint32_t>(land.data(),0);return id&&id!=UINT32_MAX;}
int today(){
    const int64_t offset=std::getenv("NX_TIME_OFFSET")?std::strtoll(std::getenv("NX_TIME_OFFSET"),nullptr,10):0;
    // The island day changes at 05:00. This reflects the runtime's existing time-travel offset.
    const time_t t=std::time(nullptr)+offset-5*3600;std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&t);
#else
    localtime_r(&t,&local);
#endif
    return local.tm_wday;
}
#ifdef _WIN32
bool read(uint64_t at,void* data,size_t size){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),data,size,&got)&&got==size;}
bool identify(uint64_t anchor,const Land& wanted,Candidate& out){
    if((anchor&3)||anchor<kHeapBase||anchor+kFruitFromLandIdentity+0x28>=kHeapBase+kHeapMax)return false;
    Land land{};if(!read(anchor,land.data(),land.size())||land!=wanted)return false;
    Record record{};if(!read(anchor+kVisitorFromLandIdentity,record.data(),record.size())||!valid_record(record))return false;
    std::array<uint8_t,0x20> weather{};if(!read(anchor+kWeatherFromLandIdentity,weather.data(),weather.size()))return false;
    const auto year=field<uint16_t>(weather.data(),0);
    if(year<2000||year>2060||weather[2]<1||weather[2]>12||weather[3]<1||weather[3]>31||weather[4]>23||weather[5]>59||weather[6]>59||field<uint32_t>(weather.data(),0x14)>1)return false;
    std::array<uint8_t,0x28> fruits{};if(!read(anchor+kFruitFromLandIdentity,fruits.data(),fruits.size())||!fruit(field<uint16_t>(fruits.data(),0))||!fruit(field<uint16_t>(fruits.data(),2))||fruits[0x24]>7||fruits[0x25]>7)return false;
    out={anchor,land,record};return true;
}
std::vector<Candidate> collect(const Land& land){
    std::vector<Candidate> out;constexpr size_t chunk=4u<<20;std::vector<uint8_t> bytes(chunk+land.size());
    uint64_t at=kHeapBase;
    while(at<kHeapBase+kHeapMax){
        MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof mbi))break;
        const uint64_t end=std::min(uint64_t(reinterpret_cast<uintptr_t>(mbi.BaseAddress))+mbi.RegionSize,kHeapBase+kHeapMax);if(end<=at)break;
        if(mbi.State==MEM_COMMIT&&!(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))){
            for(uint64_t part=at;part<end;part+=chunk){const size_t count=size_t(std::min(uint64_t(bytes.size()),end-part));if(!read(part,bytes.data(),count))continue;
                const uint8_t* next=bytes.data();const uint8_t* stop=bytes.data()+count;
                while(size_t(stop-next)>=land.size()){
                    const auto* hit=static_cast<const uint8_t*>(std::memchr(next,land[0],size_t(stop-next)-land.size()+1));if(!hit)break;next=hit+1;
                    if(std::memcmp(hit,land.data(),land.size()))continue;
                    const uint64_t anchor=part+size_t(hit-bytes.data());
                    if(std::any_of(out.begin(),out.end(),[&](const Candidate& c){return c.anchor==anchor;}))continue;
                    Candidate c;if(identify(anchor,land,c))out.push_back(c);
                }
            }
        }at=end;
    }return out;
}
bool exchange(uint64_t at,int32_t before,int32_t after){
    if(at&3)return false;
    __try{return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),LONG(after),LONG(before))==LONG(before);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#endif
bool apply_schedule(const Request& request,std::string& message){
    auto& s=store();std::lock_guard lock(s.lock);
#ifdef _WIN32
    if(!supported()||s.scanning||s.candidates.empty()||request.land!=s.selected){message="Find visitors for this island first.";return false;}
    struct Change {uint64_t address;int32_t before,after;};std::vector<Change> changes;
    for(const auto& c:s.candidates){Candidate live;if(!identify(c.anchor,request.land,live)){message="Island data changed. Find visitors again.";return false;}
        std::vector<Patch> planned;if(!plan(live.record,request.visitor,request.day,planned,message))return false;
        for(const auto& p:planned)changes.push_back({c.anchor+kVisitorFromLandIdentity+p.offset,p.before,p.after});
    }
    if(changes.empty()){message="This visitor is already scheduled. Save, then reload your island.";return true;}
    FILE* file=plat::fopen_utf8(plat::data_root()+"/visitors-menu-undo.log","a");if(!file){message="Could not open the visitor undo journal. Nothing changed.";return false;}
    for(const auto& c:changes)std::fprintf(file,"visitor %s day %d address %llx before %08x after %08x\n",visitor_name(request.visitor),request.day,(unsigned long long)c.address,uint32_t(c.before),uint32_t(c.after));
    bool journal=std::fflush(file)==0;if(std::fclose(file)!=0)journal=false;if(!journal){message="Could not flush the visitor undo journal. Nothing changed.";return false;}
    size_t applied=0;
    for(const auto& c:changes){if(!exchange(c.address,c.before,c.after)){for(size_t i=0;i<applied;++i)exchange(changes[i].address,changes[i].after,changes[i].before);message="The game updated its visitors. Refresh and retry.";return false;}++applied;}
    for(const auto& c:changes){int32_t value=0;if(!read(c.address,&value,4)||value!=c.after){message="The game updated its visitor schedule. Refresh before retrying.";return false;}}
    for(auto& c:s.candidates){Candidate live;if(identify(c.anchor,request.land,live))c=live;}
    message=request.day<0?std::string(visitor_name(request.visitor))+" removed from this week's special schedule.":std::string(visitor_name(request.visitor))+" scheduled for "+day_name(request.day)+". Save, then reload your island.";
    log("visitors menu: %s day %d verified (%zu checked fields, %zu island records)",visitor_name(request.visitor),request.day,changes.size(),s.candidates.size());return true;
#else
    message="Visitor controls support this Windows build.";return false;
#endif
}
}
const char* visitor_name(Visitor visitor){switch(visitor){case Visitor::None:return "No daytime visitor";case Visitor::Gulliver:return "Gulliver";case Visitor::Label:return "Label";case Visitor::Saharah:return "Saharah";case Visitor::Wisp:return "Wisp";case Visitor::Mabel:return "Mabel";case Visitor::CJ:return "C.J.";case Visitor::Flick:return "Flick";case Visitor::Kicks:return "Kicks";case Visitor::Leif:return "Leif";case Visitor::Redd:return "Redd";case Visitor::Gullivarrr:return "Gullivarrr";case Visitor::Celeste:return "Celeste";}return "Unknown visitor";}
const char* day_name(int day){static const char* names[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};return day>=0&&day<7?names[day]:"None";}
State state(){auto& s=store();std::lock_guard lock(s.lock);State out;out.supported=supported();out.scanning=s.scanning;out.pending=s.processing||s.request.has_value();out.message=s.message;out.today=today();out.copies=unsigned(s.candidates.size());if(!s.candidates.empty()){const auto& r=s.candidates.front().record;for(size_t i=0;i<7;++i)out.daily[i]=field<int32_t>(r.data(),i*4);out.wisp_day=field<int32_t>(r.data(),0x70);out.celeste_day=field<int32_t>(r.data(),0x74);}return out;}
void scan(const std::string& key){Land land{};if(!land_from_key(key,land))return;auto& s=store();{std::lock_guard lock(s.lock);if(!supported()||s.processing||s.request||s.scanning.exchange(true))return;s.message="Finding this island's visitor schedule...";s.selected=land;}
    std::thread([land]{std::vector<Candidate> found;
#ifdef _WIN32
        try{found=collect(land);}catch(...){}
#endif
        auto& s=store();std::lock_guard lock(s.lock);s.candidates=std::move(found);s.scanning=false;s.message=s.candidates.empty()?"No island schedule found. Enter your home island, then try again.":"Ready. Choose a visitor and weekday. Save and reload after scheduling.";log("visitors menu: found %zu validated island records",s.candidates.size());}).detach();
}
bool request_schedule(const std::string& key,Visitor visitor,int day,std::string& message){Land land{};if(!land_from_key(key,land)){message="Choose your loaded player first.";return false;}auto& s=store();std::lock_guard lock(s.lock);if(!supported()||s.scanning||s.processing||s.request){message="Wait for the visitor operation to finish.";return false;}if(s.candidates.empty()||land!=s.selected){message="Find visitors for this island first.";return false;}std::vector<Patch> changes;if(!plan(s.candidates.front().record,visitor,day,changes,message))return false;s.request=Request{land,visitor,day};s.message=message="Updating the visitor schedule...";s.signal=true;return true;}
void process_pending(){auto& s=store();if(!s.signal.exchange(false))return;Request request;{std::lock_guard lock(s.lock);if(!s.request)return;request=*s.request;s.request.reset();s.processing=true;}std::string message;try{apply_schedule(request,message);}catch(...){message="The visitor schedule could not be updated. Find visitors again.";}std::lock_guard lock(s.lock);s.processing=false;s.message=std::move(message);}
} // namespace nx::visitors
