#pragma once
#include <array>
#include "include/config.h"
#include "include/type.h"

namespace sats::logic
{
    // Output-stationary systolic array: A rows enter at column 0 and shift right, B columns enter at
    // row 0 and shift down, and PE(i, j) accumulates the product of whatever passes through it into
    // psum[buf][i][j]. Two psum buffers let one chain accumulate while the other is drained.
    class PEArrayLogic
    {
    public:
        PEArrayLogic();
        void shift_in(const type::InputVector &a_vec, const type::InputVector &b_vec, size_t buf);
        type::AccVector shift_out(size_t row_index, size_t buf);

    private:
        std::array<type::AccVector, config::DIM> psum[2];
        std::array<type::InputVector, config::DIM> a_reg;
        std::array<type::InputVector, config::DIM> b_reg;
    };
}
