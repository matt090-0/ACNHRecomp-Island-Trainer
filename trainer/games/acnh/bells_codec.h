#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

// ACNH 3.x's eight-byte currency format and pocket metadata, documented by NHSE:
// https://github.com/kwsch/NHSE/blob/master/NHSE.Core/Encryption/EncryptedInt32.cs
// https://github.com/kwsch/NHSE/blob/master/NHSE.Core/Editing/Inventory/PlayerItemSet.cs
// This changes live currency only; it does not edit encrypted save files.
namespace nx::bells {
constexpr uint32_t kWalletMax = 99999;
constexpr uint32_t kBankMax = 999999999;
constexpr uint32_t kMilesMax = 9999999;
constexpr uint32_t kTotalMilesMax = 0x7fffffff;
constexpr uint64_t kBankFromPockets = 0x2d68c;
constexpr uint64_t kMilesBeforePockets = 0x37bf0 - 0x12718;
inline uint8_t checksum(uint32_t word) {
    return uint8_t((word & 255) + ((word >> 8) & 255) + ((word >> 16) & 255) + (word >> 24) - 0x2d);
}
inline bool decode(uint64_t raw, uint32_t& value) {
    const auto word = uint32_t(raw);
    if (uint8_t(raw >> 56) != checksum(word)) return false;
    const auto adjust = uint16_t(raw >> 32);
    const auto shift = uint8_t(raw >> 48);
    value = std::rotr(word, (unsigned(shift) + 3) & 31) + uint32_t(0x80e32b11) - adjust;
    return true;
}
inline uint64_t encode(uint64_t previous, uint32_t value) {
    const auto adjust = uint16_t(previous >> 32);
    const auto shift = uint8_t(previous >> 48);
    const auto word = std::rotl(value + adjust - uint32_t(0x80e32b11), (unsigned(shift) + 3) & 31);
    return uint64_t(word) | (uint64_t(adjust) << 32) | (uint64_t(shift) << 48) | (uint64_t(checksum(word)) << 56);
}
template<class T> inline T word_at(const uint8_t* bytes, unsigned at) {
    T result{};
    std::memcpy(&result, bytes + at, sizeof result);
    return result;
}
inline bool valid_pockets(const uint8_t* bytes) {
    const auto extra = word_at<uint32_t>(bytes, 0xa0);
    if ((extra != 0 && extra != 10 && extra != 20) || word_at<uint32_t>(bytes, 0x158) != 20) return false;
    unsigned bindings = 0;
    for (unsigned at : {0xa4u, 0x15cu}) {
        for (unsigned i = 0; i < 20; ++i) {
            const unsigned b = bytes[at + i];
            if (b == 255) continue;
            if (b >= 8 || (bindings & (1u << b))) return false;
            bindings |= 1u << b;
        }
    }
    for (unsigned at : {0u, 0xb8u})
        for (unsigned i = 0; i < 20; ++i) {
            const auto item = word_at<uint16_t>(bytes, at + i * 8);
            if (item != 0xfffe && item > 0x7fff) return false;
        }
    return true;
}
} // namespace nx::bells
