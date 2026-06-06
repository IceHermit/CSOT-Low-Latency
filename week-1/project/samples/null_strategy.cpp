#include "strategy.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {


class NullStrategy : public csot::Strategy {

public:
    std::vector<csot::Order> on_tick(const csot::Tick& t) noexcept override {
        return {};
    }

    void on_fill(const csot::Order& o, double, std::uint32_t fill_qty) override {
        return;
    }

private:

};
}  // namespace

extern "C" csot::Strategy* create_strategy() {
    return new NullStrategy();
}
