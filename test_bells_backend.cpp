#include "runtime-source/games/acnh/bells_menu.h"
#include "runtime-source/games/acnh/bells_codec.h"
#include "runtime-source/games/acnh/item_catalog.h"
#include <nx/runtime.h>
#include <windows.h>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <thread>

namespace { std::string root; }
namespace nx::plat { std::string data_root() { return root; } }
namespace nx::plat { FILE* fopen_utf8(const std::string& p,const char*) {return _wfopen(std::filesystem::u8path(p).c_str(),L"a");} }
namespace nx { void log(const char* format,...) { va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts(""); } }
template<class T> void put(uint64_t at,T value) { std::memcpy(reinterpret_cast<void*>(at),&value,sizeof value); }
uint64_t get64(uint64_t at) { uint64_t value=0;std::memcpy(&value,reinterpret_cast<void*>(at),8);return value; }
void fixture(uint64_t at) {
    using namespace nx::bells;
    auto* p=reinterpret_cast<uint8_t*>(at);
    for(unsigned start:{0u,0xb8u}) for(unsigned i=0;i<20;++i) put<uint64_t>(at+start+i*8,0xfffe);
    put<uint32_t>(at+0x158,20);
    for(unsigned start:{0xa4u,0x15cu}) std::memset(p+start,255,20);
    put<uint64_t>(at+0x170,encode(0xd31ad537cfe3c48aull,31280));
    put<uint64_t>(at+0x2d68c,encode(0xd31ad537cfe3c48aull,0));
    put<uint64_t>(at-kMilesBeforePockets,encode(0xd31ad537cfe3c48aull,550));
    put<uint64_t>(at-kMilesBeforePockets+8,encode(0xd31ad537cfe3c48aull,550));
    constexpr auto delta=0x37bf0-0xc248;
    put<uint32_t>(at-delta,0x12345678);
    put<uint32_t>(at-delta+0x1c,0x12345679);
    std::memcpy(reinterpret_cast<void*>(at-delta+4),L"TestIsland",20);
    std::memcpy(reinterpret_cast<void*>(at-delta+0x20),L"TestPlayer",20);
}
int main(int argc,char** argv) {
    using namespace nx::bells;
    assert(argc>=2);
    root=argv[1];
    std::filesystem::create_directories(root);
    {std::ofstream f(root+"/game.ini");f<<"build_id = FCD2BB238ABE99E925B4E452DF1F41F1\n";}
    {std::ofstream f(root+"/mod-menu-items.tsv");f<<"ACNH_MENU_ITEMS_V1\nI\t1\t30\t0\tTest\tTest stack\nV\t0\tDefault\nI\t9617\t1\t1\tTools\tGolden axe\nV\t0\tDefault\n";}
    assert(state().supported);
    const auto heap=uint64_t(nx::kHeapBase);
    void* memory=VirtualAlloc(reinterpret_cast<void*>(heap),4<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(memory==reinterpret_cast<void*>(heap));
    const uint64_t a=heap+0x100000,b=heap+0x180000;
    fixture(a);fixture(b);
    scan();
    for(int i=0;i<500 && state().scanning;++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto found=state();
    assert(!found.scanning && found.players.size()==1 && found.players[0].copies==2);
    const auto key=found.players[0].key;
    std::string message;
    assert(grant(key,10000,false,message));
    uint32_t value=0;
    assert(decode(get64(a+0x170),value) && value==41280);
    assert(decode(get64(b+0x170),value) && value==41280);
    assert(grant(key,100000,false,message));
    assert(decode(get64(a+0x170),value) && value==99999);
    assert(grant(key,0,true,message));
    assert(decode(get64(b+0x170),value) && value==99999);
    // The third bank word crosses a cache-line boundary and is only four-byte aligned.
    const uint64_t c=heap+0x200030;
    fixture(c);
    scan();
    for(int i=0;i<500 && state().scanning;++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(state().players.size()==1 && state().players[0].copies==3);
    const uint64_t wallet_a=get64(a+0x170),wallet_c=get64(c+0x170);
    for(uint64_t p:{a,b,c}) {
        put<uint32_t>(p+kBankFromPockets-4,0xaabbccdd);
        put<uint32_t>(p+kBankFromPockets+8,0x11223344);
    }
    uint32_t expected_bank=0;
    for(uint32_t amount:{100u,1000u,100000u,100000000u}) {
        const uint64_t before_bank=get64(a+kBankFromPockets);
        assert(request_grant(key,Currency::Bank,amount,false,message));
        assert(state().pending && get64(a+kBankFromPockets)==before_bank);
        assert(!request_grant(key,Currency::Bank,100,false,message));
        process_pending();
        expected_bank+=amount;
        assert(!state().pending);
        for(uint64_t p:{a,b,c}) {
            assert(decode(get64(p+kBankFromPockets),value) && value==expected_bank);
            assert(word_at<uint32_t>(reinterpret_cast<uint8_t*>(p),unsigned(kBankFromPockets-4))==0xaabbccdd);
            assert(word_at<uint32_t>(reinterpret_cast<uint8_t*>(p),unsigned(kBankFromPockets+8))==0x11223344);
        }
    }
    assert(get64(a+0x170)==wallet_a && get64(c+0x170)==wallet_c);
    uint32_t expected_miles=550;
    for(uint32_t amount:{100u,1000u,100000u}) {
        assert(request_grant(key,Currency::Miles,amount,false,message));
        process_pending();
        expected_miles+=amount;
        for(uint64_t p:{a,b,c}) {
            assert(decode(get64(p-kMilesBeforePockets),value) && value==expected_miles);
            assert(decode(get64(p-kMilesBeforePockets+8),value) && value==expected_miles);
            assert(decode(get64(p+kBankFromPockets),value) && value==expected_bank);
        }
    }
    for(uint64_t p:{a,b,c}) {
        put<uint64_t>(p+kBankFromPockets,encode(get64(p+kBankFromPockets),kBankMax-50));
        put<uint64_t>(p-kMilesBeforePockets,encode(get64(p-kMilesBeforePockets),kMilesMax-50));
        put<uint64_t>(p-kMilesBeforePockets+8,encode(get64(p-kMilesBeforePockets+8),50000000));
    }
    assert(request_grant(key,Currency::Bank,100,false,message));process_pending();
    assert(decode(get64(c+kBankFromPockets),value) && value==kBankMax);
    assert(request_grant(key,Currency::Miles,100,false,message));process_pending();
    assert(decode(get64(a-kMilesBeforePockets),value) && value==kMilesMax);
    assert(decode(get64(a-kMilesBeforePockets+8),value) && value==50000050);
    // Copies can contain different occupied slots. Only a common empty UNLOCKED slot is eligible.
    put<uint64_t>(a+0xb8,123);put<uint64_t>(b+0xb8+8,456);
    assert(request_item(key,1,30,0,message));
    assert(get64(a+0xb8+16)==0xfffe); // queued, no render-thread write
    process_pending();
    const uint64_t stack=uint64_t(1)|(uint64_t(29)<<32);
    for(uint64_t p:{a,b,c})assert(get64(p+0xb8+16)==stack);
    assert(get64(a+0xb8)==123 && get64(b+0xb8+8)==456);
    assert(!request_item(key,1,31,0,message));
    assert(!request_item(key,0xffff,1,0,message));
    assert(!request_item(key,1,1,1,message));
    // Repair changes tool use count alone, preserving colour/flags/slot bindings and all neighbours.
    const uint64_t worn=uint64_t(9617)|(uint64_t(0x55)<<16)|(uint64_t(3)<<32)|(uint64_t(17)<<48);
    for(uint64_t p:{a,b,c}) {put<uint64_t>(p+0xb8+24,worn);put<uint8_t>(p+0x15c+3,2);}
    assert(request_tools(key,ToolAction::Repair,message));process_pending();
    for(uint64_t p:{a,b,c}) {
        assert(get64(p+0xb8+24)==(worn&0x0000ffffffffffffull));
        assert(word_at<uint8_t>(reinterpret_cast<uint8_t*>(p),0x15c+3)==2);
        assert(get64(p+0xb8+16)==stack && get64(p+0xb8+32)==0xfffe);
    }
    for(uint64_t p:{a,b,c})for(unsigned i=0;i<20;++i)put<uint64_t>(p+0xb8+i*8,123);
    // Extra twenty slots are empty but locked, so a full basic inventory must reject the item.
    assert(request_item(key,1,1,0,message));process_pending();
    for(uint64_t p:{a,b,c}) {assert(get64(p)==0xfffe);for(unsigned i=0;i<20;++i)assert(get64(p+0xb8+i*8)==123);}
    const auto valid_b_miles=get64(b-kMilesBeforePockets);
    put<uint64_t>(b-kMilesBeforePockets,valid_b_miles^0x0100000000000000ull);
    const auto a_miles_before=get64(a-kMilesBeforePockets);
    assert(request_grant(key,Currency::Miles,100,false,message));process_pending();
    assert(get64(a-kMilesBeforePockets)==a_miles_before && !state().pending);
    put<uint64_t>(b-kMilesBeforePockets,valid_b_miles);
    assert(!request_grant(key,static_cast<Currency>(9),100,false,message));
    const auto before=get64(a+0x170);
    put<uint64_t>(b+0x170,get64(b+0x170)^0x0100000000000000ull);
    assert(!grant(key,10000,false,message));
    assert(get64(a+0x170)==before);
    put<uint64_t>(b+0x170,before);
    put<uint32_t>(b-(0x37bf0-0xc248)+0x1c,0xaaaaaaaa);
    assert(!grant(key,10000,false,message));
    assert(get64(a+0x170)==before);
    assert(!grant("unknown character",10000,false,message));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    assert(format_amount(100000000)=="100,000,000");
    puts("Backend: currencies, queued inventory grants, occupied/locked slot protection, stacks, tool repair and metadata preservation passed.");
}
