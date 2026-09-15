#include "memory_arbiter.h"
#include "include/type.h"
#include "utility/request_tag.h"

namespace sats
{
    MemoryArbiter::MemoryArbiter(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        i_socket.register_nb_transport_bw(this, &MemoryArbiter::nb_transport_bw);
        t_socket.register_nb_transport_fw(this, &MemoryArbiter::nb_transport_fw);
    }

    tlm::tlm_sync_enum MemoryArbiter::nb_transport_fw(int initiator_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_REQ)
        {
            size_t bank_id = type::SpadAddr::from_flat(payload.get_address()).bank;

            const auto &tag = utility::tag_of(payload);
            std::string detail = "initiator=" + std::to_string(initiator_id) + ",bank=" + std::to_string(bank_id) +
                                 ",row=" + std::to_string(tag.row);

            if (bank_busy[bank_id])
            {
                tracer.log("memory_arbiter", tag.instr_id, "queued", detail);
                pending_requests[bank_id].push({initiator_id, &payload});
                return tlm::TLM_ACCEPTED;
            }

            bank_busy[bank_id] = true;
            bank_to_initiator_id[bank_id] = initiator_id;
            tracer.log("memory_arbiter", tag.instr_id, "forward", detail);

            return i_socket[bank_id]->nb_transport_fw(payload, phase, delay);
        }
        else
        {
            SC_REPORT_ERROR("MemoryArbiter", "Invalid phase in nb_transport_fw");
            return tlm::TLM_ACCEPTED;
        }
    }

    tlm::tlm_sync_enum MemoryArbiter::nb_transport_bw(int bank_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_RESP)
        {
            int initiator_id = bank_to_initiator_id[bank_id];

            const auto &tag = utility::tag_of(payload);
            tracer.log("memory_arbiter", tag.instr_id, "complete",
                       "initiator=" + std::to_string(initiator_id) + ",bank=" + std::to_string(bank_id) +
                           ",row=" + std::to_string(tag.row));

            tlm::tlm_sync_enum sync = t_socket[initiator_id]->nb_transport_bw(payload, phase, delay);

            if (pending_requests[bank_id].empty())
            {
                bank_busy[bank_id] = false;
            }
            else
            {
                PendingRequest pending = pending_requests[bank_id].front();
                pending_requests[bank_id].pop();

                bank_to_initiator_id[bank_id] = pending.initiator_id;

                const auto &pending_tag = utility::tag_of(*pending.payload);
                tracer.log("memory_arbiter", pending_tag.instr_id, "forward",
                           "initiator=" + std::to_string(pending.initiator_id) + ",bank=" + std::to_string(bank_id) +
                               ",row=" + std::to_string(pending_tag.row));

                tlm::tlm_phase new_req_phase = tlm::BEGIN_REQ;
                sc_core::sc_time new_req_delay = sc_core::SC_ZERO_TIME;
                i_socket[bank_id]->nb_transport_fw(*pending.payload, new_req_phase, new_req_delay);
            }

            return sync;
        }
        else
        {
            SC_REPORT_ERROR("MemoryArbiter", "Invalid phase in nb_transport_bw");
            return tlm::TLM_ACCEPTED;
        }
    }
}
