#include "pe_array_module.h"
#include "include/config.h"

namespace sats
{
    PEArrayModule::PEArrayModule(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        SC_THREAD(gemm_thread);
        SC_THREAD(drain_thread);
    }

    void PEArrayModule::gemm_thread()
    {
        while (true)
        {
            auto cmd = gemm_cmd->read();

            for (size_t i = 0; i < cmd.rows; ++i)
            {
                type::InputVector a_vec = a_in->read();
                type::InputVector b_vec = b_in->read();
                pe_array_logic.shift_in(a_vec, b_vec, cmd.psum_index);
                tracer.log("pe_array_gemm", cmd.instr_id, "tick", "buf=" + std::to_string(cmd.psum_index));
                wait(config::PE_MAC_TIME);
            }
        }
    }

    void PEArrayModule::drain_thread()
    {
        while (true)
        {
            auto cmd = drain_cmd->read();

            for (size_t i = 0; i < cmd.rows; ++i)
            {
                type::AccVector c_vec = pe_array_logic.shift_out(i, cmd.psum_index);
                c_out->write(c_vec);
                tracer.log("pe_array_drain", cmd.instr_id, "tick", "buf=" + std::to_string(cmd.psum_index));
                wait(config::PE_DRAIN_TIME);
            }
        }
    }
}
