#include "scaler_logic.h"
#include "include/config.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace sats::logic
{
    type::InputVector ScalerLogic::shift(const type::AccVector& vec, type::ScaleFactor scale_factor)
    {
        type::InputVector out{};

        for (size_t i = 0; i < config::DIM; i++)
            out[i] = scale(vec[i], scale_factor);

        return out;
    }

    config::inputType ScalerLogic::scale(config::accType value, type::ScaleFactor scale_factor)
    {
        static_assert(sizeof(config::accType) == 4, "the int64 product assumes a 32-bit accType");

        // value * mult / 2^(31 + shift), rounded half up
        int total_shift = std::numeric_limits<config::accType>::digits + scale_factor.shift;
        int64_t prod = static_cast<int64_t>(value) * scale_factor.mult;
        int64_t rounded = (prod + (int64_t{1} << (total_shift - 1))) >> total_shift;

        // accType -> inputType, saturating
        return static_cast<config::inputType>(std::clamp<int64_t>(rounded,
            std::numeric_limits<config::inputType>::min(),
            std::numeric_limits<config::inputType>::max()));
    }
}
