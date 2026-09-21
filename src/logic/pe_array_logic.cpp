#include "pe_array_logic.h"
#include <type_traits>

namespace sats::logic
{
    PEArrayLogic::PEArrayLogic()
    {
        for (size_t buf = 0; buf < 2; ++buf)
            for (auto &row : psum[buf])
                row.fill(0);
        for (auto &row : a_reg)
            row.fill(0);
        for (auto &row : b_reg)
            row.fill(0);
    }

    void PEArrayLogic::shift_in(const type::InputVector &a_vec, const type::InputVector &b_vec, size_t buf)
    {
        for (size_t i = 0; i < config::DIM; i++)
            for (size_t j = config::DIM - 1; j > 0; j--)
                a_reg[i][j] = a_reg[i][j - 1];
        for (size_t j = 0; j < config::DIM; j++)
            for (size_t i = config::DIM - 1; i > 0; i--)
                b_reg[i][j] = b_reg[i - 1][j];

        for (size_t i = 0; i < config::DIM; i++)
            a_reg[i][0] = a_vec[i];
        for (size_t j = 0; j < config::DIM; j++)
            b_reg[0][j] = b_vec[j];

        // Adds in unsigned so an overflow wraps around instead of being undefined behaviour.
        for (size_t i = 0; i < config::DIM; i++)
            for (size_t j = 0; j < config::DIM; j++)
                psum[buf][i][j] = static_cast<config::accType>(
                    static_cast<std::make_unsigned_t<config::accType>>(psum[buf][i][j]) +
                    static_cast<std::make_unsigned_t<config::accType>>(static_cast<config::accType>(a_reg[i][j]) * static_cast<config::accType>(b_reg[i][j])));
    }

    type::AccVector PEArrayLogic::shift_out(size_t row_index, size_t buf)
    {
        type::AccVector &row = psum[buf][row_index];
        type::AccVector out = row;
        row.fill(0);
        return out;
    }
}
