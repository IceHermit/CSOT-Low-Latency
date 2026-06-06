#include "strategy.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
inline constexpr std::size_t WINDOW = 64;
inline constexpr double ENTRY_Z_SQ = 4.0;
inline constexpr double EXIT_Z_SQ = 0.25;
inline constexpr double EPSILON_STDDEV_SQ = 1e-18;

struct alignas(64) SymbolState {
    std::int32_t position = 0;
    std::uint16_t count = 0;
    std::uint16_t head = 0;
    double sum = 0.0;
    double sumsq = 0.0;
};

alignas(64) double mids[64][WINDOW]{0};

class SpecStrategy : public csot::Strategy {

public:
    std::vector<csot::Order> on_tick(const csot::Tick& t) noexcept override {

        int id = get_id(t.symbol.data());
        SymbolState& st = states[id];

        const double mid = (t.bid_px + t.ask_px) * 0.5;

        st.sum += mid;
        st.sumsq += mid*mid;

        if (st.count == WINDOW) [[likely]]
        {
            const double old_mid = mids[id][st.head];
            st.sum -= old_mid;
            st.sumsq -= old_mid*old_mid;
        }

        mids[id][st.head] = mid;
        st.head = (st.head + 1) & 63;

        if (st.count < WINDOW) [[unlikely]]
            ++st.count;

        if (st.count < WINDOW) [[unlikely]]
            return {};

        const double mean = st.sum * 0.015625;
        const double variance = (st.sumsq * 0.015625) - mean*mean;

        if (variance < EPSILON_STDDEV_SQ) [[unlikely]]
        {
            return {};
        }

        const double z_sq_var = (mid - mean)*(mid - mean);

        if ((st.position == 0))
        {
            if (z_sq_var >= ENTRY_Z_SQ * variance)
            {
                if (mid > mean)
                    return {csot::Order{csot::Order::Side::SELL, t.symbol, t.bid_px, 1}};
                else
                    return {csot::Order{csot::Order::Side::BUY, t.symbol, t.ask_px, 1}};
            }
            else
                return {};
        }

        if (z_sq_var > EXIT_Z_SQ * variance){
            return {};
        }

        if (st.position > 0) {
            return {csot::Order{
                csot::Order::Side::SELL, t.symbol, t.bid_px,
                static_cast<std::uint32_t>(st.position)}};
        }

        else {
            return {csot::Order{
                csot::Order::Side::BUY, t.symbol, t.ask_px,
                static_cast<std::uint32_t>(-st.position)}};
        }
    }

    void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) override {
        int id = get_id(o.symbol.data());
        SymbolState& st = states[id];

        if (o.side == csot::Order::Side::BUY) {
            st.position += static_cast<std::int32_t>(fill_qty);
        } else {
            st.position -= static_cast<std::int32_t>(fill_qty);
        }
    }

private:
    std::array<const char*, 64> symbols{};
    std::array<SymbolState, 64> states;
    size_t num_symbols = 0;

    __attribute__((always_inline)) inline int get_id(const char* target_ptr) noexcept {

        #pragma GCC unroll 64
        for (size_t i = 0; i < 64; ++i) {
            if (symbols[i] == target_ptr) {
                return static_cast<int>(i);
            }

            if (symbols[i] == nullptr) {
                symbols[i] = target_ptr;
                num_symbols = i + 1;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

}  // namespace

extern "C" csot::Strategy* create_strategy() {
    return new SpecStrategy();
}

