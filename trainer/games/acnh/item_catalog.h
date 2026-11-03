#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nx::items {
struct Variant { std::string name; uint32_t free_param = 0; };
struct Entry {
    uint16_t id = 0;
    std::string name, category;
    uint16_t max_stack = 1;
    bool tool = false;
    std::vector<Variant> variants;
};
// A local cache generated from this player's own RomFS. No game data is embedded.
const std::vector<Entry>& catalog();
const Entry* find(uint16_t id);
const std::string& catalog_error();
bool make_item(uint16_t id, unsigned count, unsigned variant, uint64_t& raw, std::string& error);
// Only the six breakable tool families are upgraded/repaired. Other records are returned unchanged.
uint64_t upgrade_tool(uint64_t raw, bool golden);
uint64_t repair_tool(uint64_t raw);
} // namespace nx::items
