#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "include/isa.h"
#include "include/type.h"
#include "include/config.h"
#include "utility/tracer.h"

namespace sats
{
    // Drives the compute pipeline. gemm_thread reads A and B rows from the scratchpad and pushes them
    // through the skew buffers into the PE array; drain_thread pulls scaled rows out of the array
    // through the scaler and writes them back to the scratchpad. The two run independently on
    // separate psum buffers.
    class ExecController : public sc_core::sc_module
    {
    public:

        tlm_utils::simple_initiator_socket<ExecController> a_socket;
        tlm_utils::simple_initiator_socket<ExecController> b_socket;
        tlm_utils::simple_initiator_socket<ExecController> c_socket;

        sc_core::sc_port<sc_core::sc_fifo_out_if<type::InputVector>> a_out;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::InputVector>> b_out;
        sc_core::sc_port<sc_core::sc_fifo_in_if<type::InputVector>> c_in;

        sc_core::sc_port<sc_core::sc_fifo_out_if<type::ExecCmd>> gemm_cmd;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::ExecCmd>> drain_cmd;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::ExecCmd>> skew_a_cmd;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::ExecCmd>> skew_b_cmd;
        sc_core::sc_port<sc_core::sc_fifo_out_if<type::ExecCmd>> scaler_cmd;

        SC_HAS_PROCESS(ExecController);
        ExecController(sc_core::sc_module_name name, utility::Tracer &tracer);

        void issue(const isa::GemmInstr &instr);
        void issue(const isa::DrainInstr &instr);
        bool has_gemm_room() const;
        bool has_drain_room() const;
        bool is_gemm_complete(size_t id) const;
        bool is_drain_complete(size_t id) const;

        sc_core::sc_event gemm_room_freed;
        sc_core::sc_event drain_room_freed;
        sc_core::sc_event gemm_completed;
        sc_core::sc_event drain_completed;

    private:
        utility::Tracer &tracer;

        tlm::tlm_sync_enum nb_transport_a_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);
        tlm::tlm_sync_enum nb_transport_b_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);
        tlm::tlm_sync_enum nb_transport_c_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);

        void gemm_thread();
        void drain_thread();

        sc_core::sc_fifo<isa::GemmInstr> gemm_instr_queue{config::INSTR_QUEUE_SIZE};
        sc_core::sc_fifo<isa::DrainInstr> drain_instr_queue{config::INSTR_QUEUE_SIZE};
        tlm::tlm_generic_payload a_payload, b_payload, c_payload;
        type::InputVector a_vec, b_vec, c_vec;   // the payloads read into / write out of these directly
        sc_core::sc_event a_done, b_done, c_done;
        // gemm and drain each run one instruction at a time in issue order, so every id at or below the
        // last finished one is complete.
        size_t last_gemm_completed_id = 0;
        size_t last_drain_completed_id = 0;
    };
}
