#pragma once
#include "include/type.h"

namespace sats::logic
{
    class ScalerLogic
    {
    public:
        type::InputVector shift(const type::AccVector& vec);
        static config::inputType scale(config::accType value);
    };
}
