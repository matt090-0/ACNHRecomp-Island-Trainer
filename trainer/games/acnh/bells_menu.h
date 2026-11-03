#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace nx::bells {
enum class Currency { Wallet, Bank, Miles };
enum class ToolAction { Repair, Upgrade, Golden };
struct Player {
    std::string key, name, island;
    uint32_t wallet = 0, bank = 0, miles = 0, total_miles = 0;
    bool miles_ready = false;
    unsigned copies = 0;
    unsigned pocket_count = 20;
    std::array<uint64_t,40> items{};
};
struct State {
    bool scanning = false, supported = false, pending = false;
    std::string message;
    std::vector<Player> players;
};
State state();
void scan();
bool grant(const std::string& player_key, uint32_t amount, bool fill, std::string& message);
// UI requests are applied when the guest next polls its controls, never on the render thread.
bool request_grant(const std::string& player_key, Currency currency, uint32_t amount, bool fill, std::string& message);
bool request_item(const std::string& player_key, uint16_t id, unsigned count, unsigned variant, std::string& message);
bool request_tools(const std::string& player_key, ToolAction action, std::string& message);
void process_pending();
const char* currency_name(Currency currency);
std::string format_amount(uint32_t value);
} // namespace nx::bells
