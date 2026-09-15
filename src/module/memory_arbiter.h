#pragma once

#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include "include/config.h"
#include <queue>
#include "utility/tracer.h"

namespace sats
{
    // One request per bank in flight; a request to a busy bank waits in that bank's FIFO and is
    // forwarded when the bank's current request completes. Banks never interfere with each other.
    class MemoryArbiter : public sc_core::sc_module
    {
    public:
        tlm_utils::multi_passthrough_initiator_socket<MemoryArbiter> i_socket;
        tlm_utils::multi_passthrough_target_socket<MemoryArbiter> t_socket;

        MemoryArbiter(sc_core::sc_module_name name, utility::Tracer &tracer);

    private:
        utility::Tracer &tracer;

        tlm::tlm_sync_enum nb_transport_fw(int initiator_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);
        tlm::tlm_sync_enum nb_transport_bw(int bank_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay);

        struct PendingRequest
        {
            int initiator_id;
            tlm::tlm_generic_payload *payload;
        };

        bool bank_busy[config::N_BANKS] = {};
        int bank_to_initiator_id[config::N_BANKS] = {};
        std::queue<PendingRequest> pending_requests[config::N_BANKS];
    };
}
