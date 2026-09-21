#include "scaler_module.h"

namespace sats
{
    ScalerModule::ScalerModule(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        SC_THREAD(thread);
    }

    void ScalerModule::thread()
    {
        while (true)
        {
            auto cmd = cmd_in->read();
            for (size_t i = 0; i < cmd.rows; ++i)
            {
                type::AccVector acc_vec = data_in->read();
                data_out->write(scaler_logic.shift(acc_vec, cmd.scale_factor));
                tracer.log("scaler_module", cmd.instr_id, "tick");
                wait(config::SCALER_SCALE_TIME);
            }
        }
    }
}
