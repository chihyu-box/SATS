#pragma once
#include "include/type.h"

namespace sats::logic
{
    class ScalerLogic
    {
    public:
        type::InputVector shift(const type::AccVector& vec, type::ScaleFactor scale_factor);
        static config::inputType scale(config::accType value, type::ScaleFactor scale_factor);
    };
}
