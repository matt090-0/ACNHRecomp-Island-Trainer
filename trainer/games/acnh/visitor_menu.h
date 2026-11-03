#pragma once
#include "visitor_plan.h"

namespace nx::visitors {
struct State {
    bool supported=false,scanning=false,pending=false;
    unsigned copies=0;
    int today=0,celeste_day=-1,wisp_day=-1;
    std::array<int,7> daily{};
    std::string message;
};
// Select using the full player key from bells::Player; only its complete land identity is matched.
void scan(const std::string& player_key);
State state();
bool request_schedule(const std::string& player_key,Visitor visitor,int weekday,std::string& message);
// Must be called on the guest input/update thread, just like bells::process_pending().
void process_pending();
const char* visitor_name(Visitor visitor);
const char* day_name(int weekday);
} // namespace nx::visitors
