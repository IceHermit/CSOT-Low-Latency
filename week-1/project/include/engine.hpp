#pragma once

#include "strategy.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <deque>
#include <unordered_map>

namespace csot {

struct Engine {
    std::vector<Tick> ticks;
    std::deque<std::string> symbol_storage;
    std::unordered_map<std::string_view, std::string_view> symbol_map;

    void load_ticks(const std::string& path);

    void run(Strategy& strategy);

private:
    std::string_view intern(const std::string& symbol);
};

} // namespace csot
