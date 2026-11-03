#include "runtime-source/games/acnh/item_catalog.h"
#include <cassert>
#include <cstdio>
#include <string>
namespace { std::string root; }
namespace nx::plat { std::string data_root() { return root; } }
int main(int argc,char** argv) {
    using namespace nx::items;
    assert(argc>=2);root=argv[1];
    const auto& all=catalog();
    if(argc==3 && std::string(argv[2])=="--expect-invalid") {assert(all.empty() && !catalog_error().empty());return 0;}
    if(!catalog_error().empty()) {std::fprintf(stderr,"%s\n",catalog_error().c_str());return 1;}
    assert(all.size()>10000);
    size_t variants=0;
    std::string error;uint64_t raw=0;
    for(const auto& e:all) {
        assert(e.id && e.id<=0x7fff && !e.name.empty() && !e.variants.empty());
        for(unsigned v=0;v<e.variants.size();++v) {
            assert(make_item(e.id,1,v,raw,error));assert(uint16_t(raw)==e.id);
            assert((raw&0xffff0000ull)==0); // no field/drop/wrapping flags in pocket gifts
            assert(make_item(e.id,e.max_stack,v,raw,error));
            if(e.max_stack>1) {assert(uint16_t(raw>>32)==e.max_stack-1);assert(uint16_t(raw>>48)==uint16_t(e.variants[v].free_param>>16));}
            ++variants;
        }
        assert(!make_item(e.id,0,0,raw,error));
        assert(!make_item(e.id,e.max_stack+1,0,raw,error));
        assert(!make_item(e.id,1,unsigned(e.variants.size()),raw,error));
    }
    assert(!find(4200) && !find(60000) && !make_item(0xfffe,1,0,raw,error));
    assert(find(5794) && find(5794)->variants.size()>=900);
    assert(make_item(11711,50,1,raw,error)); // coloured stack of simple wooden fence
    assert(uint16_t(raw>>32)==49 && uint16_t(raw>>48)==1);
    for(const auto chain: {std::initializer_list<uint16_t>{3068,5856,2100,9617}, {3084,2377,8660}, {2376,5784,8179}, {3082,2099,8574}, {3083,2379,8533}, {2822,8578}}) {
        const auto last=*(chain.end()-1);
        for(auto it=chain.begin();it!=chain.end();++it) {
            const uint64_t flags=0x55030000ull;
            const uint64_t used=uint64_t(*it)|flags|(uint64_t(42)<<48);
            assert(repair_tool(used)==(used&0x0000ffffffffffffull));
            const auto gold=upgrade_tool(used,true);
            if(*it==last) assert(gold==used);
            else {assert(uint16_t(gold)==last);assert((gold&0xffff0000ull)==flags);assert(!(gold>>32));}
            if(it+1!=chain.end()) assert(uint16_t(upgrade_tool(used,false))==*(it+1));
        }
    }
    const uint64_t ordinary=0x123456789abc0100ull;
    assert(repair_tool(ordinary)==ordinary && upgrade_tool(ordinary,true)==ordinary);
    std::printf("Catalog checks passed: %zu items, %zu variations, six tool families.\n",all.size(),variants);
}
