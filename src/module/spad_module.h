#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include "logic/spad_logic.h"
#include "include/config.h"

namespace sats
{
    // One target socket and one thread per bank; a bank serves one request at a time and takes
    // BANK_ACCESS_TIME for it.
    class SpadModule : public sc_core::sc_module
    {
    public:
        tlm_utils::simple_target_socket_tagged<SpadModule> t_socket[config::N_BANKS];

        SpadModule(sc_core::sc_module_name name);

    private:
        tlm::tlm_sync_enum nb_transport_fw(int bank_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);

        void bank_thread(size_t bank_id);

        logic::SpadLogic spad_logic;
        sc_core::sc_event bank_events[config::N_BANKS];
        tlm::tlm_generic_payload *bank_to_payload[config::N_BANKS] = {nullptr};
    };
}
