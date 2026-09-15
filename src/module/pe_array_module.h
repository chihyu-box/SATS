#pragma once

#include <systemc>
#include "logic/pe_array_logic.h"
#include "include/type.h"
#include "utility/tracer.h"

namespace sats
{
    class PEArrayModule : public sc_core::sc_module
    {
    public:
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::InputVector>> a_in;
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::InputVector>> b_in;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::AccVector>> c_out;
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::ExecCmd>> gemm_cmd;
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::ExecCmd>> drain_cmd;
        SC_HAS_PROCESS(PEArrayModule);
        PEArrayModule(sc_core::sc_module_name name, utility::Tracer &tracer);

    private:
        logic::PEArrayLogic pe_array_logic;
        utility::Tracer &tracer;

        void gemm_thread();
        void drain_thread();
    };
}
