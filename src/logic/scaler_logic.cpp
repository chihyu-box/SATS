#include "scaler_logic.h"
#include "include/config.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sats::logic
{
    type::InputVector ScalerLogic::shift(const type::AccVector& vec)
    {
        type::InputVector out{};

        for (size_t i = 0; i < config::DIM; i++)
            out[i] = scale(vec[i]);

        return out;
    }

    config::inputType ScalerLogic::scale(config::accType value)
    {
        constexpr double out_max = std::numeric_limits<config::inputType>::max();
        constexpr double out_min = std::numeric_limits<config::inputType>::min();

        double val = std::round(static_cast<double>(value) * config::SCALE_FACTOR);
        return static_cast<config::inputType>(std::clamp(val, out_min, out_max));
    }
}
