#include "spad_module.h"
#include <cstring>

namespace sats
{
    SpadModule::SpadModule(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
    {
        for (size_t bank_id = 0; bank_id < config::N_BANKS; bank_id++)
        {
            t_socket[bank_id].register_nb_transport_fw(this, &SpadModule::nb_transport_fw, bank_id);

            sc_core::sc_spawn([this, bank_id]()
                              { this->bank_thread(bank_id); });
        }
    }

    tlm::tlm_sync_enum SpadModule::nb_transport_fw(int bank_id, tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_REQ)
        {
            if (bank_to_payload[bank_id] != nullptr)
            {
                SC_REPORT_ERROR("SpadModule", "Bank conflict");
            }

            bank_to_payload[bank_id] = &payload;
            bank_events[bank_id].notify(sc_core::SC_ZERO_TIME);

            phase = tlm::END_REQ;
            return tlm::TLM_UPDATED;
        }
        else
        {
            SC_REPORT_ERROR("SpadModule", "Invalid phase in nb_transport_fw");
        }

        return tlm::TLM_ACCEPTED;
    }

    void SpadModule::bank_thread(size_t bank_id)
    {
        while (true)
        {
            wait(bank_events[bank_id]);

            tlm::tlm_generic_payload *payload = bank_to_payload[bank_id];
            tlm::tlm_command cmd = payload->get_command();
            type::SpadAddr addr = type::SpadAddr::from_flat(payload->get_address());
            unsigned char *data_ptr = payload->get_data_ptr();

            if (cmd == tlm::TLM_READ_COMMAND)
            {
                auto row_data = spad_logic.read_row(addr);
                std::memcpy(data_ptr, row_data.data(), config::BYTES_PER_BANK_ROW);
            }
            else if (cmd == tlm::TLM_WRITE_COMMAND)
            {
                type::InputVector row_data;
                std::memcpy(row_data.data(), data_ptr, config::BYTES_PER_BANK_ROW);
                spad_logic.write_row(addr, row_data);
            }
            else
            {
                SC_REPORT_ERROR("SpadModule", "Invalid command in bank_thread");
            }

            wait(config::BANK_ACCESS_TIME);

            bank_to_payload[bank_id] = nullptr;

            tlm::tlm_phase phase = tlm::BEGIN_RESP;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            t_socket[bank_id]->nb_transport_bw(*payload, phase, delay);
        }
    }
}
