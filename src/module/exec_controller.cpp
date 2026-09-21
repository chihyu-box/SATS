#include "exec_controller.h"
#include "utility/request_tag.h"

namespace sats
{
    ExecController::ExecController(sc_core::sc_module_name name, utility::Tracer &tracer)
        : sc_core::sc_module(name), tracer(tracer)
    {
        SC_THREAD(gemm_thread);
        SC_THREAD(drain_thread);

        a_socket.register_nb_transport_bw(this, &ExecController::nb_transport_a_bw);
        b_socket.register_nb_transport_bw(this, &ExecController::nb_transport_b_bw);
        c_socket.register_nb_transport_bw(this, &ExecController::nb_transport_c_bw);

        a_payload.set_command(tlm::TLM_READ_COMMAND);
        a_payload.set_data_ptr(a_vec.bytes());
        a_payload.set_data_length(config::BYTES_PER_BANK_ROW);

        b_payload.set_command(tlm::TLM_READ_COMMAND);
        b_payload.set_data_ptr(b_vec.bytes());
        b_payload.set_data_length(config::BYTES_PER_BANK_ROW);

        c_payload.set_command(tlm::TLM_WRITE_COMMAND);
        c_payload.set_data_ptr(c_vec.bytes());
        c_payload.set_data_length(config::BYTES_PER_BANK_ROW);

        for (auto *payload : {&a_payload, &b_payload, &c_payload})
            payload->set_extension(new utility::RequestTag);
    }

    void ExecController::issue(const isa::GemmInstr &instr)
    {
        gemm_instr_queue.write(instr);
    }

    void ExecController::issue(const isa::DrainInstr &instr)
    {
        drain_instr_queue.write(instr);
    }

    bool ExecController::has_gemm_room() const
    {
        return gemm_instr_queue.num_free() > 0;
    }

    bool ExecController::has_drain_room() const
    {
        return drain_instr_queue.num_free() > 0;
    }

    bool ExecController::is_gemm_complete(size_t id) const
    {
        return id <= last_gemm_completed_id;
    }

    bool ExecController::is_drain_complete(size_t id) const
    {
        return id <= last_drain_completed_id;
    }

    tlm::tlm_sync_enum ExecController::nb_transport_a_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_RESP)
        {
            const auto &tag = utility::tag_of(payload);
            tracer.log("exec_controller_gemm", tag.instr_id, "a_spad_resp", "row=" + std::to_string(tag.row));
            a_done.notify(sc_core::SC_ZERO_TIME);
            return tlm::TLM_COMPLETED;
        }
        else
        {
            SC_REPORT_ERROR("ExecController", "Unexpected phase in nb_transport_a_bw");
            return tlm::TLM_ACCEPTED;
        }
    }

    tlm::tlm_sync_enum ExecController::nb_transport_b_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_RESP)
        {
            const auto &tag = utility::tag_of(payload);
            tracer.log("exec_controller_gemm", tag.instr_id, "b_spad_resp", "row=" + std::to_string(tag.row));
            b_done.notify(sc_core::SC_ZERO_TIME);
            return tlm::TLM_COMPLETED;
        }
        else
        {
            SC_REPORT_ERROR("ExecController", "Unexpected phase in nb_transport_b_bw");
            return tlm::TLM_ACCEPTED;
        }
    }

    tlm::tlm_sync_enum ExecController::nb_transport_c_bw(tlm::tlm_generic_payload &payload, tlm::tlm_phase &phase, sc_core::sc_time &delay)
    {
        if (phase == tlm::BEGIN_RESP)
        {
            const auto &tag = utility::tag_of(payload);
            tracer.log("exec_controller_drain", tag.instr_id, "c_spad_resp", "row=" + std::to_string(tag.row));
            c_done.notify(sc_core::SC_ZERO_TIME);
            return tlm::TLM_COMPLETED;
        }
        else
        {
            SC_REPORT_ERROR("ExecController", "Unexpected phase in nb_transport_c_bw");
            return tlm::TLM_ACCEPTED;
        }
    }

    void ExecController::gemm_thread()
    {
        while (true)
        {
            auto instr = gemm_instr_queue.read();
            tracer.log("exec_controller_gemm", instr.instr_id, "dequeue");
            gemm_room_freed.notify(sc_core::SC_ZERO_TIME);

            // A flush pushes zeros until the last data row has crossed the skew buffers (DIM-1 shifts)
            // and the array (DIM-1 more).
            size_t rows = instr.is_flush ? 2 * config::DIM - 2 : config::DIM;
            gemm_cmd->write({.psum_index = instr.psum_index, .rows = rows, .instr_id = instr.instr_id});
            skew_a_cmd->write({.rows = rows, .instr_id = instr.instr_id});
            skew_b_cmd->write({.rows = rows, .instr_id = instr.instr_id});
            tracer.log("exec_controller_gemm", instr.instr_id, "begin", "buf=" + std::to_string(instr.psum_index));

            if (instr.is_flush)
            {
                // A zero row goes out every cycle, the same pace the gemm loop reaches when it waits a
                // cycle for each row to come back from the scratchpad, so the stream into the skew
                // buffers never carries two rows in one cycle when a flush starts right after the
                // last data row of the gemm it closes.
                type::InputVector zero{};
                for (size_t row = 0; row < rows; ++row)
                {
                    wait(config::CYCLE_TIME);
                    a_out->write(zero);
                    b_out->write(zero);
                }
                // The last row enters the array the cycle it is written; its MAC lands in psum one
                // PE_MAC_TIME later, which is when the chain is complete.
                wait(config::PE_MAC_TIME);
            }
            else
            {
                for (size_t row = 0; row < rows; ++row)
                {
                    a_payload.set_address(instr.A_addr.to_flat() + row * instr.a_stride);
                    b_payload.set_address(instr.B_addr.to_flat() + row * instr.b_stride);
                    for (auto *payload : {&a_payload, &b_payload})
                    {
                        auto &tag = utility::tag_of(*payload);
                        tag.instr_id = instr.instr_id;
                        tag.row = row;
                    }

                    tracer.log("exec_controller_gemm", instr.instr_id, "a_spad_req", "row=" + std::to_string(row));
                    tlm::tlm_phase a_phase = tlm::BEGIN_REQ;
                    sc_core::sc_time a_delay = sc_core::SC_ZERO_TIME;
                    a_socket->nb_transport_fw(a_payload, a_phase, a_delay);

                    tracer.log("exec_controller_gemm", instr.instr_id, "b_spad_req", "row=" + std::to_string(row));
                    tlm::tlm_phase b_phase = tlm::BEGIN_REQ;
                    sc_core::sc_time b_delay = sc_core::SC_ZERO_TIME;
                    b_socket->nb_transport_fw(b_payload, b_phase, b_delay);

                    wait(a_done & b_done);

                    a_out->write(a_vec);
                    b_out->write(b_vec);
                }
            }

            last_gemm_completed_id = instr.instr_id;
            tracer.log("exec_controller_gemm", instr.instr_id, "end");
            gemm_completed.notify(sc_core::SC_ZERO_TIME);
        }
    }

    void ExecController::drain_thread()
    {
        while (true)
        {
            auto instr = drain_instr_queue.read();
            tracer.log("exec_controller_drain", instr.instr_id, "dequeue");
            drain_room_freed.notify(sc_core::SC_ZERO_TIME);

            drain_cmd->write({.psum_index = instr.psum_index, .rows = config::DIM, .instr_id = instr.instr_id});
            scaler_cmd->write({.scale_factor = instr.scale_factor, .rows = config::DIM, .instr_id = instr.instr_id});
            tracer.log("exec_controller_drain", instr.instr_id, "begin", "buf=" + std::to_string(instr.psum_index));

            for (size_t row = 0; row < config::DIM; ++row)
            {
                c_vec = c_in->read();

                c_payload.set_address(instr.C_addr.to_flat() + row * instr.c_stride);
                auto &tag = utility::tag_of(c_payload);
                tag.instr_id = instr.instr_id;
                tag.row = row;
                tracer.log("exec_controller_drain", instr.instr_id, "c_spad_req", "row=" + std::to_string(row));
                tlm::tlm_phase phase = tlm::BEGIN_REQ;
                sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
                c_socket->nb_transport_fw(c_payload, phase, delay);

                wait(c_done);
            }

            last_drain_completed_id = instr.instr_id;
            tracer.log("exec_controller_drain", instr.instr_id, "end");
            drain_completed.notify(sc_core::SC_ZERO_TIME);
        }
    }
}
