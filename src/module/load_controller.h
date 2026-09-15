#pragma once

#include <systemc>
#include "include/isa.h"
#include "include/config.h"
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include "utility/dummy_memory_manager.h"
#include "utility/tracer.h"
#include <queue>
#include <unordered_map>

namespace sats
{
    // Moves tiles from DRAM into the scratchpad one row at a time. dram_read_thread issues a DRAM read
    // for every row it can find a free buffer for; spad_write_thread writes each buffer to the
    // scratchpad as soon as its data has landed and a credit is free, then returns both to their pools.
    class LoadController : public sc_core::sc_module
    {
    public:
        tlm_utils::simple_initiator_socket<LoadController> dram_i_socket;
        tlm_utils::simple_initiator_socket<LoadController> spad_i_socket;

        SC_HAS_PROCESS(LoadController);
        LoadController(sc_core::sc_module_name name, utility::Tracer &tracer);

        void issue(const isa::MvinInstr &instr);
        bool has_room() const;
        bool is_complete(size_t id) const;

        sc_core::sc_event room_freed;
        sc_core::sc_event completed;

    private:
        sc_core::sc_event buf_freed;      // a data buffer went back to the pool
        sc_core::sc_event buf_ready;      // a data buffer holds a row for the next phase
        sc_core::sc_event credit_freed;   // a scratchpad credit went back to the pool

        utility::Tracer &tracer;
        utility::DummyMemoryManager dummy_memory_manager;

        tlm::tlm_sync_enum nb_dram_transport_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);
        tlm::tlm_sync_enum nb_spad_transport_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);

        void dram_read_thread();
        void spad_write_thread();

        sc_core::sc_fifo<isa::MvinInstr> mvin_instr_queue{config::INSTR_QUEUE_SIZE};

        type::InputVector row_buffers[config::LOAD_N_DATA_BUF];
        tlm::tlm_generic_payload dram_payloads[config::LOAD_N_DATA_BUF];
        tlm::tlm_generic_payload spad_payloads[config::SPAD_CREDIT_SIZE];
        std::queue<size_t> free_data_bufs;
        std::queue<size_t> free_spad_credits;
        std::queue<size_t> ready_bufs;

        // Rows of one instruction can return from DRAM in any order, and the last row of instruction
        // N+1 can land before the last row of N, so completion is counted per instruction rather than
        // by the highest finished id.
        std::unordered_map<size_t, size_t> rows_remaining;

        size_t last_issued_id = 0;
    };
}
