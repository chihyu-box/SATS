#pragma once
#include <array>
#include <deque>
#include "include/config.h"
#include "include/type.h"

namespace sats::logic
{
    // Delays lane i by i shifts so that row r of A and column r of B meet at PE(i, j) on the same
    // shift, which is what lets the array multiply without any per-PE addressing.
    class SkewBufLogic
    {
    public:
        SkewBufLogic();
        type::InputVector shift(const type::InputVector &vec);

    private:
        std::array<std::deque<config::inputType>, config::DIM> regs;
    };
}
