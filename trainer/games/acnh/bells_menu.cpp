#include "bells_menu.h"
#include "bells_codec.h"
#include "item_catalog.h"
#include "platform.h"
#include <nx/runtime.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace nx::bells {
namespace {
struct Candidate {
    uint64_t pockets = 0, wallet = 0;
    std::array<uint8_t, 0x38> identity{};
    Player player;
};
struct Request {
    enum class Kind { Currency, Item, Tools } kind = Kind::Currency;
    std::string key;
    Currency currency = Currency::Wallet;
    uint32_t amount = 0;
    bool fill = false;
    uint16_t item = 0;
    unsigned count = 1, variant = 0;
    ToolAction tool = ToolAction::Repair;
};
struct Store {
    std::mutex lock;
    std::atomic<bool> scanning{false};
    std::atomic<bool> pending_signal{false};
    bool processing = false;
    std::optional<Request> request;
    std::string message = "Choose Find player after loading your island.";
    std::vector<Candidate> candidates;
};
// Process-lifetime state: a detached, bounded read-only scan must not race static destruction on quit.
Store& store() { static Store* s = new Store; return *s; }
constexpr uint64_t kIdBeforePockets = 0x37bf0 - 0xc248;
bool supported() {
    static const bool ok = [] {
        std::ifstream in(std::filesystem::u8path(plat::data_root() + "/game.ini"));
        std::string text((std::istreambuf_iterator<char>(in)), {});
        return text.find("FCD2BB238ABE99E925B4E452DF1F41F1") != std::string::npos;
    }();
    return ok;
}
#ifdef _WIN32
bool read(uint64_t address, void* out, size_t bytes) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, bytes, &got) && got == bytes;
}
std::string name(const uint8_t* p) {
    wchar_t text[11]{};
    std::memcpy(text, p, 20);
    unsigned length = 0;
    while (length < 10 && text[length]) {
        if (text[length] < 32 || (text[length] >= 0xd800 && text[length] <= 0xdfff)) return {};
        ++length;
    }
    if (!length) return {};
    char utf8[48]{};
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, int(length), utf8, sizeof utf8, nullptr, nullptr);
    return bytes > 0 ? std::string(utf8, size_t(bytes)) : std::string{};
}
bool identify(uint64_t pockets, Candidate& c) {
    if ((pockets & 7) || pockets < kHeapBase + kIdBeforePockets || pockets + 0x2d694 >= kHeapBase + kHeapMax) return false;
    std::array<uint8_t,0x178> data{};
    if (!read(pockets, data.data(), data.size()) || !valid_pockets(data.data())) return false;
    uint32_t wallet = 0, bank = 0;
    uint64_t raw_bank = 0;
    if (!decode(word_at<uint64_t>(data.data(),0x170),wallet) || wallet > kWalletMax) return false;
    if (!read(pockets+kBankFromPockets,&raw_bank,8) || !decode(raw_bank,bank) || bank > kBankMax) return false;
    if (!read(pockets-kIdBeforePockets,c.identity.data(),c.identity.size())) return false;
    uint32_t island_id = word_at<uint32_t>(c.identity.data(),0), player_id = word_at<uint32_t>(c.identity.data(),0x1c);
    if (!island_id || !player_id || island_id == UINT32_MAX || player_id == UINT32_MAX) return false;
    c.player.island = name(c.identity.data()+4);
    c.player.name = name(c.identity.data()+0x20);
    if (c.player.name.empty() || c.player.island.empty()) return false;
    // Keep the complete identity, rather than names alone: different residents can have the same display name.
    c.player.key.assign(reinterpret_cast<const char*>(c.identity.data()),c.identity.size());
    c.player.wallet = wallet;
    c.player.bank = bank;
    c.player.pocket_count=20+word_at<uint32_t>(data.data(),0xa0);
    c.player.items.fill(0xfffe);
    for(unsigned i=0;i<c.player.pocket_count;++i)
        c.player.items[i]=word_at<uint64_t>(data.data(),i<20 ? 0xb8+i*8 : (i-20)*8);
    uint64_t miles[2]{};
    c.player.miles_ready = read(pockets-kMilesBeforePockets,miles,sizeof miles)
        && decode(miles[0],c.player.miles) && decode(miles[1],c.player.total_miles)
        && c.player.miles<=kMilesMax && c.player.total_miles<=kTotalMilesMax
        && c.player.total_miles>=c.player.miles;
    c.pockets = pockets;
    c.wallet = pockets+0x170;
    return true;
}
bool exchange(uint64_t address, uint64_t before, uint64_t after) {
    // Aligned wallet/Miles fields use a conditional atomic update. The ABD field is only
    // four-byte aligned: copy exactly eight bytes on the guest input thread, preserving its neighbours.
    if (address & 7) {
        uint64_t current=0;
        SIZE_T wrote=0;
        return read(address,&current,8) && current==before
            && WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&after,8,&wrote) && wrote==8;
    }
    __try {
        return uint64_t(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(address),LONG64(after),LONG64(before))) == before;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::vector<Candidate> collect() {
    std::vector<Candidate> found;
    constexpr size_t chunk = 4u << 20;
    std::vector<uint8_t> bytes(chunk+0x200);
    uint64_t at = kHeapBase;
    while (at < kHeapBase+kHeapMax) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof mbi)) break;
        uint64_t end = std::min(uint64_t(reinterpret_cast<uintptr_t>(mbi.BaseAddress))+mbi.RegionSize,kHeapBase+kHeapMax);
        if (end <= at) break;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD|PAGE_NOACCESS))) {
            for (uint64_t part = at; part < end; part += chunk) {
                size_t size = size_t(std::min(uint64_t(bytes.size()),end-part));
                if (!read(part,bytes.data(),size)) continue;
                const uint8_t* next = bytes.data();
                const uint8_t* stop = bytes.data()+size;
                while (next < stop) {
                    const auto* hit = static_cast<const uint8_t*>(std::memchr(next,0x14,size_t(stop-next)));
                    if (!hit) break;
                    next = hit+1;
                    size_t offset = size_t(hit-bytes.data());
                    if (offset < 0x158 || offset+0x20 >= size || word_at<uint32_t>(hit,0) != 20) continue;
                    uint64_t pockets = part+offset-0x158;
                    if (std::any_of(found.begin(),found.end(),[&](const Candidate& c){return c.pockets == pockets;})) continue;
                    Candidate c;
                    if (identify(pockets,c)) found.push_back(std::move(c));
                }
            }
        }
        at = end;
    }
    return found;
}
#endif
} // namespace

State state() {
    auto& s = store();
    std::lock_guard lock(s.lock);
    State out{s.scanning.load(),supported(),s.request.has_value() || s.processing,s.message,{}};
    std::map<std::string,Player> players;
    for (const auto& c : s.candidates) {
        auto& p = players[c.player.key];
        if (!p.copies) p=c.player;
        p.wallet=std::max(p.wallet,c.player.wallet);
        p.bank=std::max(p.bank,c.player.bank);
        p.miles=std::max(p.miles,c.player.miles);
        p.total_miles=std::max(p.total_miles,c.player.total_miles);
        p.miles_ready=p.miles_ready && c.player.miles_ready;
        ++p.copies;
    }
    for (auto& [key,p] : players) out.players.push_back(std::move(p));
    return out;
}
void scan() {
    auto& s = store();
    {
        std::lock_guard lock(s.lock);
        if (!supported() || s.request || s.processing || s.scanning.exchange(true)) return;
        s.message = "Finding your loaded character...";
    }
    std::thread([] {
        auto& s = store();
        std::vector<Candidate> found;
#ifdef _WIN32
        try { found = collect(); } catch (...) {}
#endif
        std::lock_guard lock(s.lock);
        s.candidates = std::move(found);
        s.message = s.candidates.empty() ? "No loaded character found. Enter your island, then try again." : "Ready. Choose a menu section.";
        s.scanning = false;
        log("bells menu: found %zu validated character records",s.candidates.size());
    }).detach();
}
const char* currency_name(Currency currency) {
    switch(currency) {
        case Currency::Wallet: return "Wallet";
        case Currency::Bank: return "ABD bank";
        case Currency::Miles: return "Nook Miles";
    }
    return "Unknown currency";
}
std::string format_amount(uint32_t value) {
    std::string out=std::to_string(value);
    for(int i=int(out.size())-3;i>0;i-=3) out.insert(size_t(i),",");
    return out;
}

bool apply_grant(const std::string& key, Currency currency, uint32_t amount, bool fill, std::string& message) {
    auto& s = store();
    std::lock_guard lock(s.lock);
    if (!supported() || s.scanning) { message="Wait for Find player to finish."; return false; }
#ifdef _WIN32
    if (currency!=Currency::Wallet && currency!=Currency::Bank && currency!=Currency::Miles) {
        message="Unknown currency. Nothing changed."; return false;
    }
    const uint32_t limit=currency==Currency::Wallet ? kWalletMax : currency==Currency::Bank ? kBankMax : kMilesMax;
    struct Change { uint64_t address,before,after; bool total=false; };
    std::vector<Change> changes;
    uint32_t displayed = 0;
    uint32_t lifetime = 0;
    for (const Candidate& c : s.candidates) {
        if (c.player.key != key) continue;
        Candidate live;
        uint64_t raw = 0;
        if (!identify(c.pockets,live) || live.identity != c.identity) {
            message="Character data changed. Choose Find player again."; return false;
        }
        if (currency==Currency::Miles && !live.player.miles_ready) {
            message="Miles are not ready for this character. Refresh after entering your island."; return false;
        }
        const uint64_t address=currency==Currency::Wallet ? c.wallet : currency==Currency::Bank
            ? c.pockets+kBankFromPockets : c.pockets-kMilesBeforePockets;
        uint32_t value = 0;
        if (!read(address,&raw,8) || !decode(raw,value) || value > limit) { message="Currency check failed. Nothing changed."; return false; }
        changes.push_back({address,raw,0});
        displayed=std::max(displayed,value);
        if (currency==Currency::Miles) {
            uint64_t total=0;
            uint32_t value_total=0;
            if(!read(address+8,&total,8) || !decode(total,value_total) || value_total<value || value_total>kTotalMilesMax) {
                message="Earned Miles check failed. Nothing changed."; return false;
            }
            lifetime=std::max(lifetime,value_total);
            changes.push_back({address+8,total,0,true});
        }
    }
    if (changes.empty()) { message="Choose Find player first."; return false; }
    const uint32_t before_value=displayed;
    displayed=fill ? limit : uint32_t(std::min(uint64_t(limit),uint64_t(displayed)+amount));
    const uint32_t total_after=uint32_t(std::min(uint64_t(kTotalMilesMax),uint64_t(lifetime)+(displayed-before_value)));
    for (auto& c:changes) c.after=encode(c.before,c.total ? total_after : displayed);
    // A local undo journal is flushed before changing any live values. No save files are patched.
    const std::string journal = plat::data_root()+"/bells-menu-undo.log";
    FILE* file = plat::fopen_utf8(journal,"a");
    if (!file) { message="Could not write the local undo journal. Nothing changed."; return false; }
    for (auto c : changes) std::fprintf(file,"%s%s %llx before %016llx after %016llx\n",currency_name(currency),c.total?" earned":"",(unsigned long long)c.address,(unsigned long long)c.before,(unsigned long long)c.after);
    bool journal_ok = std::fflush(file)==0;
    if (std::fclose(file)!=0) journal_ok=false;
    if (!journal_ok) { message="Could not save the undo journal. Nothing changed."; return false; }
    size_t applied = 0;
    for (auto c : changes) {
        if (!exchange(c.address,c.before,c.after)) {
            for (size_t i=0;i<applied;++i) exchange(changes[i].address,changes[i].after,changes[i].before);
            message="The game updated this balance. Refresh the player and retry.";
            return false;
        }
        ++applied;
    }
    for (const auto& change : changes) {
        uint64_t raw=0;
        if (!read(change.address,&raw,8) || raw!=change.after) {
            message="The game changed the balance during the grant. Refresh the player before trying again.";
            log("bells menu: currency readback changed after grant");
            return false;
        }
    }
    for (auto& c : s.candidates) if (c.player.key==key) {
        Candidate live;
        if(identify(c.pockets,live) && live.identity==c.identity) c.player=std::move(live.player);
    }
    message = std::string(currency_name(currency))+": "+format_amount(displayed)+(currency==Currency::Miles ? " Miles." : " Bells.");
    s.message=message;
    log("bells menu: %s grant verified, %u (%zu checked fields)",currency_name(currency),displayed,changes.size());
    return true;
#else
    message="This Bells menu build supports Windows."; return false;
#endif
}
bool grant(const std::string& key,uint32_t amount,bool fill,std::string& message) {
    return apply_grant(key,Currency::Wallet,amount,fill,message);
}
bool request_grant(const std::string& key,Currency currency,uint32_t amount,bool fill,std::string& message) {
    auto& s=store();
    std::lock_guard lock(s.lock);
    if (!supported() || s.scanning || s.processing || s.request) { message="Wait for the current operation to finish.";return false; }
    if(currency!=Currency::Wallet && currency!=Currency::Bank && currency!=Currency::Miles) {message="Unknown currency.";return false;}
    if(std::none_of(s.candidates.begin(),s.candidates.end(),[&](const Candidate& c){return c.player.key==key;})) {
        message="Find your loaded character first.";return false;
    }
    Request request;
    request.key=key;request.currency=currency;request.amount=amount;request.fill=fill;
    s.request=std::move(request);
    message=std::string("Updating ")+currency_name(currency)+"...";
    s.message=message;
    s.pending_signal=true;
    return true;
}
namespace {
bool request_inventory(Request request,std::string& message) {
    auto& s=store();
    std::lock_guard lock(s.lock);
    if(!supported() || s.scanning || s.processing || s.request) { message="Wait for the current operation to finish.";return false; }
    if(std::none_of(s.candidates.begin(),s.candidates.end(),[&](const Candidate& c){return c.player.key==request.key;})) {
        message="Find your loaded character first.";return false;
    }
    s.request=std::move(request);
    message="Updating pockets...";s.message=message;s.pending_signal=true;
    return true;
}
bool apply_inventory(const Request& request,std::string& message) {
    auto& s=store();std::lock_guard lock(s.lock);
#ifdef _WIN32
    std::vector<Candidate> players;
    for(const auto& c:s.candidates) if(c.player.key==request.key) {
        Candidate live;
        if(!identify(c.pockets,live) || live.identity!=c.identity) {message="Character data changed. Refresh your player.";return false;}
        players.push_back(std::move(live));
    }
    if(players.empty()) {message="Find your loaded character first.";return false;}
    struct Change {uint64_t address,before,after;};
    std::vector<Change> changes;
    unsigned slot=40;
    if(request.kind==Request::Kind::Item) {
        uint64_t raw=0;
        if(!items::make_item(request.item,request.count,request.variant,raw,message)) return false;
        // A slot must be unlocked and empty in EVERY validated copy. Never overwrite an item.
        for(unsigned i=0;i<40;++i) if(std::all_of(players.begin(),players.end(),[&](const Candidate& c){
            return i<c.player.pocket_count && uint16_t(c.player.items[i])==0xfffe;
        })) {slot=i;break;}
        if(slot==40) {message="No shared empty pocket slot. Close pockets, refresh and try again.";return false;}
        for(const auto& c:players) changes.push_back({c.pockets+(slot<20 ? 0xb8+slot*8 : (slot-20)*8),c.player.items[slot],raw});
    } else {
        for(const auto& c:players) for(unsigned i=0;i<c.player.pocket_count;++i) {
            const auto before=c.player.items[i];
            const auto after=request.tool==ToolAction::Repair ? items::repair_tool(before)
                : items::upgrade_tool(before,request.tool==ToolAction::Golden);
            if(before!=after) changes.push_back({c.pockets+(i<20 ? 0xb8+i*8 : (i-20)*8),before,after});
        }
        if(changes.empty()) {message="No eligible pocket tools need this change.";return false;}
    }
    FILE* file=plat::fopen_utf8(plat::data_root()+"/bells-menu-undo.log","a");
    if(!file) {message="Could not write the undo journal. Nothing changed.";return false;}
    for(auto c:changes) std::fprintf(file,"inventory %llx before %016llx after %016llx\n",(unsigned long long)c.address,(unsigned long long)c.before,(unsigned long long)c.after);
    bool ok=std::fflush(file)==0;if(std::fclose(file)!=0)ok=false;
    if(!ok) {message="Could not save the undo journal. Nothing changed.";return false;}
    size_t applied=0;
    for(const auto& c:changes) {
        if(!exchange(c.address,c.before,c.after)) {
            for(size_t i=0;i<applied;++i) exchange(changes[i].address,changes[i].after,changes[i].before);
            message="Pockets changed during the operation. Close pockets and refresh.";return false;
        }
        ++applied;
    }
    for(const auto& c:changes) {uint64_t raw=0;if(!read(c.address,&raw,8) || raw!=c.after) {
        message="The game changed pockets. Refresh before trying again.";return false;
    }}
    for(auto& c:s.candidates) if(c.player.key==request.key) {
        Candidate live;if(identify(c.pockets,live)&&live.identity==c.identity)c=std::move(live);
    }
    message=request.kind==Request::Kind::Item ? "Added to pocket slot "+std::to_string(slot+1)+". Reopen pockets to see it."
        : "Pocket tools updated. Reopen pockets before equipping them.";
    if(request.kind==Request::Kind::Item)log("mod menu: inventory verified (%zu fields), item %u slot %u",changes.size(),request.item,slot+1);
    else log("mod menu: tool action %u verified (%zu fields)",unsigned(request.tool),changes.size());
    return true;
#else
    message="This menu supports Windows.";return false;
#endif
}
} // namespace
bool request_item(const std::string& key,uint16_t id,unsigned count,unsigned variant,std::string& message) {
    uint64_t raw=0;if(!items::make_item(id,count,variant,raw,message))return false;
    Request r;r.kind=Request::Kind::Item;r.key=key;r.item=id;r.count=count;r.variant=variant;
    return request_inventory(std::move(r),message);
}
bool request_tools(const std::string& key,ToolAction action,std::string& message) {
    if(action!=ToolAction::Repair && action!=ToolAction::Upgrade && action!=ToolAction::Golden) {message="Unknown tool action.";return false;}
    Request r;r.kind=Request::Kind::Tools;r.key=key;r.tool=action;
    return request_inventory(std::move(r),message);
}
void process_pending() {
    auto& s=store();
    if(!s.pending_signal.exchange(false)) return;
    Request request;
    {
        std::lock_guard lock(s.lock);
        if(!s.request) return;
        request=std::move(*s.request);
        s.request.reset();
        s.processing=true;
    }
    std::string message;
    try {
        if(request.kind==Request::Kind::Currency) apply_grant(request.key,request.currency,request.amount,request.fill,message);
        else apply_inventory(request,message);
    }
    catch(...) { message="This grant could not be completed. Refresh your player."; }
    std::lock_guard lock(s.lock);
    s.processing=false;
    s.message=std::move(message);
}
} // namespace nx::bells
