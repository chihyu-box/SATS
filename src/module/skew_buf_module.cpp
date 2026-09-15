#include "skew_buf_module.h"
#include "include/config.h"
#include "include/type.h"

namespace sats
{
    SkewBufModule::SkewBufModule(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        SC_THREAD(run_thread);
    }

    void SkewBufModule::run_thread()
    {
        while (true)
        {
            auto cmd = cmd_in->read();
            for (size_t i = 0; i < cmd.rows; ++i)
            {
                type::InputVector in_vec = data_in->read();
                data_out->write(skew_buf_logic.shift(in_vec));
                tracer.log(basename(), cmd.instr_id, "tick");
                wait(config::SKEW_BUF_SHIFT_TIME);
            }
        }
    }
}
