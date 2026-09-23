#pragma once

#include <systemc>
#include "logic/scaler_logic.h"
#include "include/type.h"
#include "utility/tracer.h"

namespace sats
{
    class ScalerModule : public sc_core::sc_module
    {
    public:
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::AccVector>> data_in;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::InputVector>> data_out;
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::ExecCmd>> cmd_in;

        SC_HAS_PROCESS(ScalerModule);
        ScalerModule(sc_core::sc_module_name name, utility::Tracer &tracer);

    private:
        logic::ScalerLogic scaler_logic;
        utility::Tracer &tracer;

        void thread();
    };
}
