#pragma once

#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "include/config.h"
#include "include/type.h"

// Maps tiles to DIM-row scratchpad slots whose rows sit stride apart and evicts the least recently
// used tile when a bank is full. The bank of a tile is the bank of its first row. Eviction does not
// wait for instructions still using the tile.
class SimpleSpadMemoryAllocator
{
public:
    using TileId = std::tuple<char, size_t, size_t>;   // matrix ('a', 'b' or 'c'), tile row, tile column

    struct Tile
    {
        sats::type::SpadAddr addr;
        bool hit;
    };

    // Cuts the whole scratchpad into tile slots for this stride; a slot is the base address of a tile.
    explicit SimpleSpadMemoryAllocator(size_t stride)
    {
        size_t spad_capacity = sats::config::N_BANKS * sats::config::N_ROWS_PER_BANK;
        size_t chunk = sats::config::DIM * stride;
        size_t num_chunk = spad_capacity / chunk;
        for (size_t c = 0; c < num_chunk; ++c)
        {
            for (size_t s = 0; s < stride; ++s)
            {
                auto base = sats::type::SpadAddr::from_flat(c * chunk + s);
                free_slots[base.bank].push_back(base);
            }
        }
    }

    // Without a bank the tile goes wherever there is the most room.
    Tile acquire(const TileId &id, std::optional<size_t> bank = std::nullopt)
    {
        if (auto it = resident.find(id); it != resident.end())
        {
            touch(id);
            return {it->second, true};
        }
        return {allocate(id, bank), false};
    }

    // The address of a tile that must already be resident.
    sats::type::SpadAddr lookup(const TileId &id)
    {
        auto it = resident.find(id);
        if (it == resident.end())
            throw std::runtime_error(std::string("SimpleSpadMemoryAllocator: tile ") + std::get<0>(id) + std::to_string(std::get<1>(id)) + std::to_string(std::get<2>(id)) + " is not resident");
        touch(id);
        return it->second;
    }

private:
    sats::type::SpadAddr allocate(const TileId &id, std::optional<size_t> bank)
    {
        auto slot = take_free_slot(bank);
        sats::type::SpadAddr base = slot ? *slot : evict_lru(bank);
        resident[id] = base;
        touch(id);
        return base;
    }

    std::optional<sats::type::SpadAddr> take_free_slot(std::optional<size_t> bank)
    {
        size_t b;
        if (!bank)
        {
            // The bank with the most free slots, so unconstrained tiles spread evenly.
            b = 0;
            for (size_t c = 1; c < sats::config::N_BANKS; ++c)
                if (free_slots[c].size() > free_slots[b].size())
                    b = c;
        }
        else
            b = *bank;

        if (free_slots[b].empty())
            return std::nullopt;
        sats::type::SpadAddr base = free_slots[b].back();
        free_slots[b].pop_back();
        return base;
    }

    sats::type::SpadAddr evict_lru(std::optional<size_t> bank)
    {
        for (auto it = lru.begin(); it != lru.end(); ++it)
        {
            sats::type::SpadAddr base = resident.at(*it);
            if (!bank || base.bank == *bank)
            {
                resident.erase(*it);
                lru.erase(it);
                return base;
            }
        }
        throw std::runtime_error("SimpleSpadMemoryAllocator: no tile can start in bank " +
                                 (bank ? std::to_string(*bank) : std::string("(any)")) + " with this stride");
    }

    void touch(const TileId &id)
    {
        std::erase(lru, id);
        lru.push_back(id);
    }

    std::array<std::vector<sats::type::SpadAddr>, sats::config::N_BANKS> free_slots;
    std::vector<TileId> lru;
    std::map<TileId, sats::type::SpadAddr> resident;
};
