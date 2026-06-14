#include "cache_sim.hpp"

#include <array>
#include <cstdint>

namespace {

class StubCacheSim final : public csot::CacheSim {
public:
    static constexpr std::size_t L1_SETS = 64, L2_SETS = 512, WAYS = 8;
    
    std::array<std::uint64_t, L1_SETS * WAYS> L1_tag;
    std::array<std::uint64_t, L2_SETS * WAYS> L2_tag;

    std::array<std::uint8_t, L1_SETS> L1_valid_mask;
    std::array<std::uint8_t, L2_SETS> L2_valid_mask;
    
    std::array<std::uint8_t, L1_SETS> L1_dirty_mask;
    std::array<std::uint8_t, L2_SETS> L2_dirty_mask;

    std::array<std::uint32_t, L1_SETS> L1_LRU;
    std::array<std::uint32_t, L2_SETS> L2_LRU;

public:
    void on_init() override {
        L1_tag.fill(0xFFFFFFFFFFFFFFFFULL);
        L2_tag.fill(0xFFFFFFFFFFFFFFFFULL);

        L1_valid_mask.fill(0); 
        L2_valid_mask.fill(0);
        //L1_dirty_mask.fill(0);
        //L2_dirty_mask.fill(0);
        
        L1_LRU.fill(0x76543210); 
        L2_LRU.fill(0x76543210);
    }

static inline __attribute__((always_inline)) std::uint32_t touch(std::uint32_t lru, std::uint32_t way) {
        std::uint32_t diff = lru ^ (way * 0x11111111U);
        std::uint32_t shift = __builtin_ctz(~(diff | (diff >> 1) | (diff >> 2) | (diff >> 3)) & 0x11111111U);
        
        std::uint32_t lower_mask = (1U << shift) - 1;
        return (lru & ~(lower_mask | (0xFU << shift))) | ((lru & lower_mask) << 4) | way;
    }

    static inline __attribute__((always_inline)) std::size_t victim_way(std::uint32_t current_lru, std::uint8_t valid_mask) {
        std::uint32_t invalid_mask = static_cast<std::uint8_t>(~valid_mask);
        std::uint32_t ctz = __builtin_ctz(invalid_mask | 0x100); 
        std::uint32_t lru_way = (current_lru >> 28) & 0xF;
        return (ctz < 8) ? ctz : lru_way;
    }

    static inline __attribute__((always_inline)) int find_matching_way(const std::uint64_t* __restrict set_tags, std::uint64_t target_tag) {
        for (int i = 0; i < 8; ++i) {
            if (set_tags[i] == target_tag) return i;
        }
        return -1;
    }

    csot::CacheStats run(const csot::MemAccess* acc, std::size_t n) override {
        csot::CacheStats s{};

        std::uint64_t last_b  = 0xFFFFFFFFFFFFFFFF; 
        std::size_t   last_s1 = 0;
        int           last_w1 = -1;

        for (std::size_t k = 0; k < n; ++k) {

            const csot::MemAccess& a = acc[k];
            const bool is_write = a.is_write;

            s.writes += is_write;
            s.reads += !is_write;

            const std::uint64_t b = (a.address >> 6);

            if (b == last_b) {
                ++s.l1_hits;
                if (is_write) {
                    L1_dirty_mask[last_s1] |= (1U << last_w1);
                }
                continue;
            }

            const std::uint64_t s1 = b & 63;
            const std::uint64_t s1_offset = s1 << 3; 
            const std::uint64_t t1 = b >> 6;
            
            int w1 = find_matching_way(&L1_tag[s1_offset], t1);

            if (w1 >= 0) {
                ++s.l1_hits;
                if (is_write)
                    L1_dirty_mask[s1] |= (1U << w1);

                L1_LRU[s1] = touch(L1_LRU[s1], w1);
                
                last_b  = b;
                last_s1 = s1;
                last_w1 = w1;
                continue;
            }

            // L1 Miss
            ++s.l1_misses;
            const std::uint64_t s2 = b & 511;
            const std::uint64_t s2_offset = s2 << 3;
            const std::uint64_t t2 = b >> 9;

            // L2 Cache Lookup
            int w2 = find_matching_way(&L2_tag[s2_offset], t2);

            if (w2 >= 0) {
                ++s.l2_hits;
                L2_LRU[s2] = touch(L2_LRU[s2], w2);
            } else {
                ++s.l2_misses;
                const std::size_t v2 = victim_way(L2_LRU[s2], L2_valid_mask[s2]);
                s.dirty_writebacks += ((L2_valid_mask[s2] & L2_dirty_mask[s2]) >> v2) & 1;

                L2_valid_mask[s2] |= (1U << v2);
                L2_dirty_mask[s2] &= ~(1U << v2);
                L2_tag[s2_offset + v2] = t2;

                L2_LRU[s2] = touch(L2_LRU[s2], v2);
            }

            // ---- L1 Eviction & Allocation ----
            const std::size_t v1 = victim_way(L1_LRU[s1], L1_valid_mask[s1]);

            if (((L1_valid_mask[s1] & L1_dirty_mask[s1]) >> v1) & 1)
            {
                const std::uint64_t bv = (L1_tag[s1_offset + v1] << 6) | s1;
                const int s2v = bv & 511;
                const std::uint64_t s2v_offset = static_cast<std::uint64_t>(s2v) << 3;
                const std::uint64_t t2v = bv >> 9;

                int w2v = find_matching_way(&L2_tag[s2v_offset], t2v);

                if (w2v >= 0) {
                    L2_dirty_mask[s2v] |= (1U << w2v);
                } else {
                    const int vv = victim_way(L2_LRU[s2v], L2_valid_mask[s2v]);
                    if ((L2_valid_mask[s2v] & (1U << vv)) && (L2_dirty_mask[s2v] & (1U << vv)))
                        ++s.dirty_writebacks;

                    L2_valid_mask[s2v] |= (1U << vv);
                    L2_dirty_mask[s2v] |= (1U << vv);
                    L2_tag[s2v_offset + vv] = t2v;

                    L2_LRU[s2v] = touch(L2_LRU[s2v], vv);
                }
            }

            L1_tag[s1_offset + v1] = t1;
            
            L1_dirty_mask[s1] = (L1_dirty_mask[s1] & ~(1U << v1)) | (static_cast<std::uint8_t>(is_write) << v1);
            L1_valid_mask[s1] |= (1U << v1);
            L1_LRU[s1] = touch(L1_LRU[s1], v1);

            last_b  = b;
            last_s1 = s1;
            last_w1 = static_cast<int>(v1);
        }
        return s;
    }
};

}  // namespace

extern "C" csot::CacheSim* create_cache_sim() {
    return new StubCacheSim();
}
