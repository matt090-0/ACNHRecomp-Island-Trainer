#include "runtime-source/games/acnh/bells_codec.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace nx::bells;
    uint32_t value=0;
    constexpr uint64_t fixture=0xd31ad537cfe3c48aull;
    assert(decode(fixture,value) && value==31280);
    assert(!decode(fixture^0x0100000000000000ull,value));
    assert(!decode(0,value));
    for(uint32_t n : {0u,1u,10000u,31280u,99999u,999999999u}) {
        const auto raw=encode(fixture,n);
        assert(decode(raw,value) && value==n);
        assert(((raw>>32)&0xffffff)==((fixture>>32)&0xffffff));
    }
    std::array<uint8_t,0x178> p{};
    for(unsigned at:{0u,0xb8u}) for(unsigned i=0;i<20;++i) {p[at+i*8]=0xfe;p[at+i*8+1]=0xff;}
    p[0x158]=20;
    for(unsigned at:{0xa4u,0x15cu}) for(unsigned i=0;i<20;++i) p[at+i]=255;
    assert(valid_pockets(p.data()));
    p[0xa4]=1;p[0x15c]=1;assert(!valid_pockets(p.data()));
    p[0x15c]=255;p[0xa0]=3;assert(!valid_pockets(p.data()));
    std::puts("Bells codec: fixture, checksum rejection, limits, metadata preservation and pocket guards passed.");
}
