#include "store_controller.h"
#include "utility/request_tag.h"

namespace sats
{
    StoreController::StoreController(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        SC_THREAD(spad_read_thread);
        SC_THREAD(dram_write_thread);

        dram_i_socket.register_nb_transport_bw(this, &StoreController::nb_dram_transport_bw);
        spad_i_socket.register_nb_transport_bw(this, &StoreController::nb_spad_transport_bw);

        for (size_t idx = 0; idx < config::STORE_N_DATA_BUF; ++idx)
        {
            dram_payloads[idx].set_mm(&dummy_memory_manager);
            dram_payloads[idx].set_command(tlm::TLM_WRITE_COMMAND);
            dram_payloads[idx].set_data_ptr(row_buffers[idx].bytes());
            dram_payloads[idx].set_data_length(config::BYTES_PER_BANK_ROW);
            dram_payloads[idx].set_extension(new utility::RequestTag);
            free_data_bufs.push(idx);
        }

        for (size_t idx = 0; idx < config::SPAD_CREDIT_SIZE; ++idx)
        {
            spad_payloads[idx].set_command(tlm::TLM_READ_COMMAND);
            spad_payloads[idx].set_data_length(config::BYTES_PER_BANK_ROW);
            spad_payloads[idx].set_extension(new utility::RequestTag);
            free_spad_credits.push(idx);
        }
    }

    void StoreController::issue(const isa::MvoutInstr &instr)
    {
        mvout_instr_queue.write(instr);
        rows_remaining[instr.instr_id] = instr.dims.rows;
        last_issued_id = instr.instr_id;
    }

    bool StoreController::has_room() const
    {
        return mvout_instr_queue.num_free() > 0;
    }

    bool StoreController::is_complete(size_t id) const
    {
        return id <= last_issued_id && rows_remaining.find(id) == rows_remaining.end();
    }

    tlm::tlm_sync_enum StoreController::nb_dram_transport_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::END_REQ)
        {
            return tlm::TLM_ACCEPTED;
        }
        else if (phase == tlm::BEGIN_RESP)
        {
            const auto &tag = utility::tag_of(payload);
            tracer.log("store_controller_dram", tag.instr_id, "dram_resp", "row=" + std::to_string(tag.row));

            free_data_bufs.push(tag.buf);
            buf_freed.notify(sc_core::SC_ZERO_TIME);

            if (--rows_remaining.at(tag.instr_id) == 0)
            {
                rows_remaining.erase(tag.instr_id);
                tracer.log("store_controller", tag.instr_id, "end");
                completed.notify(sc_core::SC_ZERO_TIME);
            }

            phase = tlm::END_RESP;
            return tlm::TLM_UPDATED;
        }
        else
        {
            SC_REPORT_ERROR("StoreController", "Unexpected phase in nb_dram_transport_bw");
            return tlm::TLM_ACCEPTED;
        }
    }

    tlm::tlm_sync_enum StoreController::nb_spad_transport_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_RESP)
        {
            const auto &tag = utility::tag_of(payload);
            tracer.log("store_controller_spad", tag.instr_id, "spad_resp", "row=" + std::to_string(tag.row));

            free_spad_credits.push(tag.credit);
            ready_bufs.push(tag.buf);
            credit_freed.notify(sc_core::SC_ZERO_TIME);
            buf_ready.notify(sc_core::SC_ZERO_TIME);

            return tlm::TLM_COMPLETED;
        }
        else
        {
            SC_REPORT_ERROR("StoreController", "Unexpected phase in nb_spad_transport_bw");
            return tlm::TLM_ACCEPTED;
        }
    }

    void StoreController::spad_read_thread()
    {
        while (true)
        {
            auto mvout = mvout_instr_queue.read();
            tracer.log("store_controller", mvout.instr_id, "dequeue");
            room_freed.notify(sc_core::SC_ZERO_TIME);

            for (size_t row = 0; row < mvout.dims.rows; ++row)
            {
                while (free_data_bufs.empty() || free_spad_credits.empty())
                    wait(buf_freed | credit_freed);

                size_t free_buf_idx = free_data_bufs.front();
                free_data_bufs.pop();

                size_t free_spad_idx = free_spad_credits.front();
                free_spad_credits.pop();

                if (row == 0)
                    tracer.log("store_controller", mvout.instr_id, "begin");

                auto &tag = utility::tag_of(spad_payloads[free_spad_idx]);
                tag.instr_id = mvout.instr_id;
                tag.row = row;
                tag.buf = free_buf_idx;
                tag.credit = free_spad_idx;
                utility::tag_of(dram_payloads[free_buf_idx]).copy_from(tag);
                spad_payloads[free_spad_idx].set_address((mvout.spad_src + row * mvout.spad_stride).to_flat());
                spad_payloads[free_spad_idx].set_data_ptr(row_buffers[free_buf_idx].bytes());
                dram_payloads[free_buf_idx].set_address(mvout.dram_dst + row * mvout.dram_stride);
                tracer.log("store_controller_spad", mvout.instr_id, "spad_req", "row=" + std::to_string(row));
                tlm::tlm_phase phase = tlm::BEGIN_REQ;
                sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
                spad_i_socket->nb_transport_fw(spad_payloads[free_spad_idx], phase, delay);
            }
        }
    }

    void StoreController::dram_write_thread()
    {
        while (true)
        {
            while (ready_bufs.empty())
                wait(buf_ready);

            size_t idx = ready_bufs.front();
            ready_bufs.pop();

            const auto &tag = utility::tag_of(dram_payloads[idx]);
            tracer.log("store_controller_dram", tag.instr_id, "dram_req", "row=" + std::to_string(tag.row));
            tlm::tlm_phase phase = tlm::BEGIN_REQ;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            dram_i_socket->nb_transport_fw(dram_payloads[idx], phase, delay);
        }
    }
}
