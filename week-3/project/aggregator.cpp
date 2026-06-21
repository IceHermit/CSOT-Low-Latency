#include "aggregate.hpp"
#include <vector>
#include <thread>
#include <algorithm>
#include <new>
#include <memory>
#include <semaphore>

#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace {

struct alignas(64) ThreadPartialTable {
    std::unique_ptr<csot::SymbolAgg[]> table;
};

class StubAggregator final : public csot::Aggregator {
    std::uint32_t num_symbols_ = 0;
    std::uint32_t num_threads_ = 1;
    
    std::vector<ThreadPartialTable> partial_tables_;
    std::vector<std::jthread> worker_threads_;

    std::vector<std::unique_ptr<std::counting_semaphore<1>>> start_semaphores_;
    std::counting_semaphore<64> done_semaphore_{0};

    const csot::AggTick* current_ticks_ = nullptr;
    std::size_t current_n_ = 0;
    bool shutdown_flag_ = false;

public:
    ~StubAggregator() {
        shutdown_flag_ = true;
        if (num_threads_ > 0) {
            for (std::uint32_t t = 0; t < num_threads_; ++t) {
                if (start_semaphores_[t]) {
                    start_semaphores_[t]->release();
                }
            }
        }
        worker_threads_.clear();
    }

    void on_init(std::uint32_t num_symbols) override {
        num_symbols_ = num_symbols;
        num_threads_ = std::max(1u, std::thread::hardware_concurrency());
        
        partial_tables_.resize(num_threads_);
        start_semaphores_.reserve(num_threads_);

        for (std::uint32_t t = 0; t < num_threads_; ++t) {
            partial_tables_[t].table = std::make_unique<csot::SymbolAgg[]>(num_symbols_);
            std::fill(&partial_tables_[t].table[0], &partial_tables_[t].table[num_symbols_], csot::SymbolAgg{0,0,0,0,0});
            start_semaphores_.push_back(std::make_unique<std::counting_semaphore<1>>(0));
        }
        
        worker_threads_.reserve(num_threads_);

        for (std::uint32_t t = 0; t < num_threads_; ++t) {
            worker_threads_.emplace_back([this, t]() {
#if defined(__linux__)
                cpu_set_t cpuset;  
                CPU_ZERO(&cpuset);
                CPU_SET(t, &cpuset);
                sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
                thread_affinity_policy_data_t policy = { static_cast<integer_t>(t) };
                pthread_policy_set(pthread_self(), THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
#endif

                auto* __restrict my_table = partial_tables_[t].table.get();

                while (true) {
                    start_semaphores_[t]->acquire();
                    if (shutdown_flag_) break;

                    std::size_t start_idx = (current_n_ * t) / num_threads_;
                    std::size_t end_idx = (current_n_ * (t + 1)) / num_threads_;
                    
                    std::size_t i = start_idx;
                    
                    for (; i + 4 <= end_idx; i += 4) {
                        const csot::AggTick& t0 = current_ticks_[i];
                        const csot::AggTick& t1 = current_ticks_[i + 1];
                        const csot::AggTick& t2 = current_ticks_[i + 2];
                        const csot::AggTick& t3 = current_ticks_[i + 3];

#if defined(__GNUC__) || defined(__clang__)
                        __builtin_prefetch(&current_ticks_[i + 16], 0, 3);
#endif

                        csot::SymbolAgg& r0 = my_table[t0.symbol_id];
                        csot::SymbolAgg& r1 = my_table[t1.symbol_id];
                        csot::SymbolAgg& r2 = my_table[t2.symbol_id];
                        csot::SymbolAgg& r3 = my_table[t3.symbol_id];

                        if (r0.count == 0) { r0.min_price = t0.price; r0.max_price = t0.price; }
                        else { if (t0.price < r0.min_price) r0.min_price = t0.price; if (t0.price > r0.max_price) r0.max_price = t0.price; }
                        r0.count += 1; r0.sum_price += t0.price; r0.sum_qty += t0.qty;

                        if (r1.count == 0) { r1.min_price = t1.price; r1.max_price = t1.price; }
                        else { if (t1.price < r1.min_price) r1.min_price = t1.price; if (t1.price > r1.max_price) r1.max_price = t1.price; }
                        r1.count += 1; r1.sum_price += t1.price; r1.sum_qty += t1.qty;

                        if (r2.count == 0) { r2.min_price = t2.price; r2.max_price = t2.price; }
                        else { if (t2.price < r2.min_price) r2.min_price = t2.price; if (t2.price > r2.max_price) r2.max_price = t2.price; }
                        r2.count += 1; r2.sum_price += t2.price; r2.sum_qty += t2.qty;

                        if (r3.count == 0) { r3.min_price = t3.price; r3.max_price = t3.price; }
                        else { if (t3.price < r3.min_price) r3.min_price = t3.price; if (t3.price > r3.max_price) r3.max_price = t3.price; }
                        r3.count += 1; r3.sum_price += t3.price; r3.sum_qty += t3.qty;
                    }

                    for (; i < end_idx; ++i) {
                        const csot::AggTick& tick = current_ticks_[i];
                        csot::SymbolAgg& r = my_table[tick.symbol_id];
                        if (r.count == 0) {
                            r.min_price = tick.price;
                            r.max_price = tick.price;
                        } else {
                            if (tick.price < r.min_price) r.min_price = tick.price;
                            if (tick.price > r.max_price) r.max_price = tick.price;
                        }
                        r.count += 1;
                        r.sum_price += tick.price;
                        r.sum_qty += tick.qty;
                    }

                    done_semaphore_.release();
                }
            });
        }
    }

    void run(const csot::AggTick* ticks, std::size_t n, csot::SymbolAgg* out) override {
        for (std::uint32_t t = 0; t < num_threads_; ++t) {
            std::fill(&partial_tables_[t].table[0], &partial_tables_[t].table[num_symbols_], csot::SymbolAgg{0, 0, 0, 0, 0});
        }

        current_ticks_ = ticks;
        current_n_ = n;

        for (std::uint32_t t = 0; t < num_threads_; ++t) {
            start_semaphores_[t]->release();
        }

        for (std::uint32_t t = 0; t < num_threads_; ++t) {
            done_semaphore_.acquire();
        }

        // Merge Phase
        for (std::uint32_t s = 0; s < num_symbols_; ++s) {
            csot::SymbolAgg merged{0, 0, 0, 0, 0};
            bool initialized = false;

            for (std::uint32_t t = 0; t < num_threads_; ++t) {
                const csot::SymbolAgg& p = partial_tables_[t].table[s];
                if (p.count == 0) continue;

                if (!initialized) {
                    merged.min_price = p.min_price;
                    merged.max_price = p.max_price;
                    initialized = true;
                } else {
                    if (p.min_price < merged.min_price) merged.min_price = p.min_price;
                    if (p.max_price > merged.max_price) merged.max_price = p.max_price;
                }
                merged.count += p.count;
                merged.sum_price += p.sum_price;
                merged.sum_qty += p.sum_qty;
            }
            out[s] = merged;
        }
    }
};

}  // namespace

extern "C" csot::Aggregator* create_aggregator() {
    return new StubAggregator();
}
