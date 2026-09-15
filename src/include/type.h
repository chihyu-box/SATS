#pragma once

#include <cstdint>
#include <array>
#include <ostream>
#include "config.h"

namespace sats::type
{
    constexpr size_t bits_for(size_t n)
    {
        size_t bits = 0;
        while ((size_t{1} << bits) < n) ++bits;
        return bits;
    }

    // A scratchpad address is a {row_offset, bank} pair. to_flat() packs it with the bank in the low
    // bits, so +1 is the next bank and +N_BANKS is the next row of the same bank; every stride in the
    // ISA counts in these flat units. A scratchpad request carries the flat value in its TLM address
    // field: it is a row index, not a byte address.
    struct SpadAddr
    {
        uint64_t row_offset : bits_for(config::N_ROWS_PER_BANK);
        uint64_t bank : bits_for(config::N_BANKS);

        bool operator==(const SpadAddr &other) const
        {
            return bank == other.bank && row_offset == other.row_offset;
        }

        uint64_t to_flat() const
        {
            return (row_offset << bits_for(config::N_BANKS)) | bank;
        }

        static SpadAddr from_flat(uint64_t flat)
        {
            return {
                .row_offset = flat >> bits_for(config::N_BANKS),
                .bank = flat & ((uint64_t{1} << bits_for(config::N_BANKS)) - 1)};
        }

        SpadAddr operator+(size_t flat_offset) const
        {
            return from_flat(to_flat() + flat_offset);
        }
    };

    struct MatrixDims
    {
        size_t rows;
        size_t cols;
    };

    // sc_fifo<T> requires operator<< on T; these payloads are never printed, so the operators stay empty.
    struct ExecCmd
    {
        size_t psum_index;   // read by the PE array only
        size_t rows;         // DIM, or 2*DIM-2 for a flush
        size_t instr_id;
        friend std::ostream &operator<<(std::ostream &os, const ExecCmd &) { return os; }
    };

    struct InputVector : public std::array<config::inputType, config::DIM>
    {
        friend std::ostream &operator<<(std::ostream &os, const InputVector &iv) { return os; }
    };

    struct AccVector : public std::array<config::accType, config::DIM>
    {
        friend std::ostream &operator<<(std::ostream &os, const AccVector &av) { return os; }
    };
}
