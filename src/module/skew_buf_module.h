#pragma once

#include <systemc>
#include "include/type.h"
#include "logic/skew_buf_logic.h"
#include "utility/tracer.h"

namespace sats
{
    class SkewBufModule : public sc_core::sc_module
    {
        public:
            sc_core::sc_port<sc_core::sc_fifo_in_if<type::InputVector>> data_in;
            sc_core::sc_port<sc_core::sc_fifo_out_if<type::InputVector>> data_out;
            sc_core::sc_port<sc_core::sc_fifo_in_if<type::ExecCmd>> cmd_in;

            SC_HAS_PROCESS(SkewBufModule);
            SkewBufModule(sc_core::sc_module_name name, utility::Tracer &tracer);

        private:
            logic::SkewBufLogic skew_buf_logic;
            utility::Tracer &tracer;

            void run_thread();
    };
}
