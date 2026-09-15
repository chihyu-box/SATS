#include "spad_logic.h"

namespace sats::logic
{
    SpadLogic::SpadLogic()
    {
        for (auto &bank : spad)
            for (auto &row : bank)
                row.fill(0);
    }

    type::InputVector SpadLogic::read_row(type::SpadAddr addr)
    {
        return spad[addr.bank][addr.row_offset];
    }

    void SpadLogic::write_row(type::SpadAddr addr, const type::InputVector &data)
    {
        spad[addr.bank][addr.row_offset] = data;
    }
}
