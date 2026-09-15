#pragma once
#include <array>
#include "include/config.h"
#include "include/type.h"

namespace sats::logic
{
    class SpadLogic
    {
    public:
        SpadLogic();
        type::InputVector read_row(type::SpadAddr addr);
        void write_row(type::SpadAddr addr, const type::InputVector &data);

    private:
        std::array<std::array<type::InputVector, config::N_ROWS_PER_BANK>, config::N_BANKS> spad;
    };
}
