#include "top_module.h"
#include <cstring>
#include <sys/mman.h>

namespace sats
{
    TopModule::TopModule(sc_core::sc_module_name name, const std::string &trace_path, const std::string &dramsys_config_path)
        : sc_core::sc_module(name), tracer(trace_path)
    {
        progress = load_ctrl.room_freed | load_ctrl.completed |
                   store_ctrl.room_freed | store_ctrl.completed |
                   exec_ctrl.gemm_room_freed | exec_ctrl.gemm_completed |
                   exec_ctrl.drain_room_freed | exec_ctrl.drain_completed;

        exec_ctrl.a_out(a_to_skew_a);
        skew_a.data_in(a_to_skew_a);

        skew_a.data_out(skew_a_to_pe_array);
        pe_array.a_in(skew_a_to_pe_array);

        exec_ctrl.b_out(b_to_skew_b);
        skew_b.data_in(b_to_skew_b);

        skew_b.data_out(skew_b_to_pe_array);
        pe_array.b_in(skew_b_to_pe_array);

        scaler.data_out(scaler_to_c);
        exec_ctrl.c_in(scaler_to_c);

        pe_array.c_out(pe_array_to_scaler);
        scaler.data_in(pe_array_to_scaler);

        exec_ctrl.gemm_cmd(gemm_cmd);
        pe_array.gemm_cmd(gemm_cmd);

        exec_ctrl.drain_cmd(drain_cmd);
        pe_array.drain_cmd(drain_cmd);

        exec_ctrl.skew_a_cmd(skew_a_cmd);
        skew_a.cmd_in(skew_a_cmd);

        exec_ctrl.skew_b_cmd(skew_b_cmd);
        skew_b.cmd_in(skew_b_cmd);
        
        exec_ctrl.scaler_cmd(scaler_cmd);
        scaler.cmd_in(scaler_cmd);

        load_ctrl.spad_i_socket(mem_arbiter.t_socket);
        store_ctrl.spad_i_socket(mem_arbiter.t_socket);
        exec_ctrl.a_socket(mem_arbiter.t_socket);
        exec_ctrl.b_socket(mem_arbiter.t_socket);
        exec_ctrl.c_socket(mem_arbiter.t_socket);

        for (size_t i = 0; i < config::N_BANKS; ++i)
        {
            mem_arbiter.i_socket(spad.t_socket[i]);
        }

        dramsys = std::make_unique<DRAMSys::DRAMSys>("dramsys", DRAMSys::Config::from_path(dramsys_config_path));
        dram_store_size = dramsys->memorySize();
        dram_store = static_cast<unsigned char *>(mmap(nullptr, dram_store_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
        dramsys->setBackingStore(dram_store);

        load_ctrl.dram_i_socket(dramsys->tSocket);
        store_ctrl.dram_i_socket(dramsys->tSocket);
    }

    static std::string kv(const char *key, uint64_t value)
    {
        return std::string(",") + key + "=" + std::to_string(value);
    }

    size_t TopModule::mvin(isa::MvinInstr instr)
    {
        check_mvin(instr);
        while (!load_ctrl.has_room())
        {
            wait(load_ctrl.room_freed);            
        }

        size_t id = ++next_instr_id;
        id_to_op[id] = isa::Op::Mvin;
        instr.instr_id = id;
        load_ctrl.issue(instr);
        tracer.log("top", id, "issue",
                   "op=mvin" + kv("spad", instr.spad_dst.to_flat()) + kv("spad_stride", instr.spad_stride) +
                       kv("rows", instr.dims.rows) + kv("dram", instr.dram_src) + kv("dram_stride", instr.dram_stride));
        return id;
    }

    size_t TopModule::mvout(isa::MvoutInstr instr)
    {
        check_mvout(instr);
        while (!store_ctrl.has_room())
        {
            wait(store_ctrl.room_freed);
        }

        size_t id = ++next_instr_id;
        id_to_op[id] = isa::Op::Mvout;
        instr.instr_id = id;
        store_ctrl.issue(instr);
        tracer.log("top", id, "issue",
                   "op=mvout" + kv("spad", instr.spad_src.to_flat()) + kv("spad_stride", instr.spad_stride) +
                       kv("rows", instr.dims.rows) + kv("dram", instr.dram_dst) + kv("dram_stride", instr.dram_stride));
        return id;
    }

    size_t TopModule::gemm(isa::GemmInstr instr)
    {
        check_gemm(instr);
        while (!exec_ctrl.has_gemm_room())
        {
            wait(exec_ctrl.gemm_room_freed);
        }

        size_t id = ++next_instr_id;
        id_to_op[id] = isa::Op::Gemm;
        instr.instr_id = id;
        exec_ctrl.issue(instr);
        if (instr.is_flush)
            tracer.log("top", id, "issue", "op=gemm_flush" + kv("psum", instr.psum_index));
        else
            tracer.log("top", id, "issue",
                       "op=gemm" + kv("a", instr.A_addr.to_flat()) + kv("a_stride", instr.a_stride) +
                           kv("b", instr.B_addr.to_flat()) + kv("b_stride", instr.b_stride) +
                           kv("psum", instr.psum_index));
        return id;
    }

    size_t TopModule::drain(isa::DrainInstr instr)
    {
        check_drain(instr);
        while (!exec_ctrl.has_drain_room())
        {
            wait(exec_ctrl.drain_room_freed);
        }
        
        size_t id = ++next_instr_id;
        id_to_op[id] = isa::Op::Drain;
        instr.instr_id = id;
        exec_ctrl.issue(instr);
        tracer.log("top", id, "issue",
                   "op=drain" + kv("c", instr.C_addr.to_flat()) + kv("c_stride", instr.c_stride) +
                       kv("psum", instr.psum_index));
        return id;
    }

    bool TopModule::has_room(isa::Op op) const
    {
        switch (op)
        {
        case isa::Op::Mvin: return load_ctrl.has_room();
        case isa::Op::Mvout: return store_ctrl.has_room();
        case isa::Op::Gemm: return exec_ctrl.has_gemm_room();
        case isa::Op::Drain: return exec_ctrl.has_drain_room();
        }
        return false;
    }

    bool TopModule::is_complete(size_t id) const
    {
        switch (id_to_op.at(id))
        {
        case isa::Op::Mvin: return load_ctrl.is_complete(id);
        case isa::Op::Mvout: return store_ctrl.is_complete(id);
        case isa::Op::Gemm: return exec_ctrl.is_gemm_complete(id);
        case isa::Op::Drain: return exec_ctrl.is_drain_complete(id);
        }
        return false;
    }

    void TopModule::wait_complete(size_t id)
    {
        while (!is_complete(id))
        {
            switch (id_to_op.at(id))
            {
            case isa::Op::Mvin: wait(load_ctrl.completed); break;
            case isa::Op::Mvout: wait(store_ctrl.completed); break;
            case isa::Op::Gemm: wait(exec_ctrl.gemm_completed); break;
            case isa::Op::Drain: wait(exec_ctrl.drain_completed); break;
            }
        }
    }

    void TopModule::dram_write_bypass(uint64_t addr, const unsigned char *data, uint64_t length)
    {
        check_dram_range(addr, length, "DRAM_WRITE_BYPASS");
        std::memcpy(dram_store + addr, data, length);
    }

    void TopModule::dram_read_bypass(uint64_t addr, unsigned char *data, uint64_t length) const
    {
        check_dram_range(addr, length, "DRAM_READ_BYPASS");
        std::memcpy(data, dram_store + addr, length);
    }

    void TopModule::check_dram_range(uint64_t addr, uint64_t length, const std::string &ctx) const
    {
        if (length > dram_store_size || addr > dram_store_size - length)
            SC_REPORT_ERROR("TopModule", (ctx + ": operation exceeds DRAM bounds").c_str());
    }

    void TopModule::check_spad_addr_range(const type::SpadAddr &addr, size_t stride, size_t count, const std::string &ctx)
    {
        if (stride == 0)
            SC_REPORT_ERROR("TopModule", (ctx + ": scratchpad stride must be positive").c_str());

        constexpr size_t spad_size = static_cast<size_t>(config::N_BANKS) * config::N_ROWS_PER_BANK;
        size_t flat_last = addr.to_flat() + (count - 1) * stride;
        if (flat_last >= spad_size)
            SC_REPORT_ERROR("TopModule", (ctx + ": operation exceeds scratchpad bounds").c_str());
    }

    void TopModule::check_mvin(const isa::MvinInstr &instr)
    {
        static const std::string ctx = "MVIN";

        if (instr.dims.rows == 0 || instr.dims.cols == 0)
            SC_REPORT_ERROR("TopModule", (ctx + ": invalid matrix dimensions").c_str());
        if (instr.dims.cols > config::DIM)
            SC_REPORT_ERROR("TopModule", (ctx + ": column size exceeds bank row size").c_str());
        if (instr.dram_stride == 0)
            SC_REPORT_ERROR("TopModule", (ctx + ": DRAM stride must be positive").c_str());
        if (instr.dram_src % config::BYTES_PER_BANK_ROW != 0)
            SC_REPORT_WARNING("TopModule", (ctx + ": DRAM address is not aligned to row size").c_str());
        if (instr.dram_stride % config::BYTES_PER_BANK_ROW != 0)
            SC_REPORT_WARNING("TopModule", (ctx + ": DRAM stride is not a multiple of row size").c_str());

        uint64_t dram_span = (instr.dims.rows - 1) * instr.dram_stride + config::BYTES_PER_BANK_ROW;
        check_dram_range(instr.dram_src, dram_span, ctx);
        check_spad_addr_range(instr.spad_dst, instr.spad_stride, instr.dims.rows, ctx);
    }

    void TopModule::check_mvout(const isa::MvoutInstr &instr)
    {
        static const std::string ctx = "MVOUT";

        if (instr.dims.rows == 0 || instr.dims.cols == 0)
            SC_REPORT_ERROR("TopModule", (ctx + ": invalid matrix dimensions").c_str());
        if (instr.dims.cols > config::DIM)
            SC_REPORT_ERROR("TopModule", (ctx + ": column size exceeds bank row size").c_str());
        if (instr.dram_stride == 0)
            SC_REPORT_ERROR("TopModule", (ctx + ": DRAM stride must be positive").c_str());
        if (instr.dram_dst % config::BYTES_PER_BANK_ROW != 0)
            SC_REPORT_WARNING("TopModule", (ctx + ": DRAM address is not aligned to row size").c_str());
        if (instr.dram_stride % config::BYTES_PER_BANK_ROW != 0)
            SC_REPORT_WARNING("TopModule", (ctx + ": DRAM stride is not a multiple of row size").c_str());

        uint64_t dram_span = (instr.dims.rows - 1) * instr.dram_stride + config::BYTES_PER_BANK_ROW;
        check_dram_range(instr.dram_dst, dram_span, ctx);
        check_spad_addr_range(instr.spad_src, instr.spad_stride, instr.dims.rows, ctx);
    }

    void TopModule::check_gemm(const isa::GemmInstr &instr)
    {
        const std::string ctx = instr.is_flush ? "GEMM_FLUSH" : "GEMM";

        if (instr.psum_index > 1)
            SC_REPORT_ERROR("TopModule", (ctx + ": psum_buf must be 0 or 1").c_str());
        if (instr.is_flush)
            return;

        check_spad_addr_range(instr.A_addr, instr.a_stride, config::DIM, ctx);
        check_spad_addr_range(instr.B_addr, instr.b_stride, config::DIM, ctx);
    }

    void TopModule::check_drain(const isa::DrainInstr &instr)
    {
        static const std::string ctx = "DRAIN";

        if (instr.psum_index > 1)
            SC_REPORT_ERROR("TopModule", (ctx + ": psum_buf must be 0 or 1").c_str());
        
        check_spad_addr_range(instr.C_addr, instr.c_stride, config::DIM, ctx);
    }
}
