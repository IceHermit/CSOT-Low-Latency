#include "../include/engine.hpp"
#include "../include/histogram.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>

namespace csot {

std::string_view Engine::intern(const std::string& symbol) {
    auto it = symbol_map.find(symbol);
    if (it != symbol_map.end()) return it->second;
    symbol_storage.push_back(symbol);
    std::string_view view = symbol_storage.back();
    symbol_map[view] = view;
    return view;
}

void Engine::load_ticks(const std::string& path) {
    std::ifstream file(path);
    std::string line, val;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        Tick t;
        std::string ts, sym, bpx, apx, bqty, aqty;
        
        std::getline(ss, ts, ',');   t.timestamp_ns = std::stoull(ts);
        std::getline(ss, sym, ',');  t.symbol = intern(sym);
        std::getline(ss, bpx, ',');  t.bid_px = std::stod(bpx);
        std::getline(ss, apx, ',');  t.ask_px = std::stod(apx);
        std::getline(ss, bqty, ','); t.bid_qty = std::stoul(bqty);
        std::getline(ss, aqty, ','); t.ask_qty = std::stoul(aqty);
        
        ticks.push_back(t);
    }
}

void Engine::run(Strategy& strategy) {
    LatencyHistogram hist;
    
    for (const auto& tick : ticks) {
        auto t1 = std::chrono::steady_clock::now();
        
        auto orders = strategy.on_tick(tick);
        
        auto t2 = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
        hist.record(duration);
        
        for (const auto& order : orders) {
            // 1. Calculate fill price and quantity using the current 'tick' and 'order'
            //    based on your STRATEGY_SPEC.md rules. 
            //    (Below is the standard aggressive top-of-book matching pattern)
            
            double fill_price = 0.0;
            uint32_t fill_qty = 0;

            if (order.side == Order::Side::BUY) { // Or however your Side enum/struct is defined
                fill_price = tick.ask_px; 
                fill_qty = std::min(order.qty, tick.ask_qty); 
            } else {
                fill_price = tick.bid_px;
                fill_qty = std::min(order.qty, tick.bid_qty);
            }

            // 2. Pass all 3 required arguments to update the strategy state
            strategy.on_fill(order, fill_price, fill_qty);
        }
    }
    
    hist.print(std::cout);
}

} // namespace csot