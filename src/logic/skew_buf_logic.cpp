#include "skew_buf_logic.h"

namespace sats::logic
{
    SkewBufLogic::SkewBufLogic()
    {
        for (size_t lane = 0; lane < config::DIM; ++lane)
            regs[lane].assign(lane, 0);
    }

    type::InputVector SkewBufLogic::shift(const type::InputVector &vec)
    {
        type::InputVector out{};
        for (size_t lane = 0; lane < config::DIM; ++lane)
        {
            auto &line = regs[lane];
            if (line.empty())
            {
                out[lane] = vec[lane];
                continue;
            }
            line.push_front(vec[lane]);
            out[lane] = line.back();
            line.pop_back();
        }
        return out;
    }
}
