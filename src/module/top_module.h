#pragma once

#include <cstddef>
#include <systemc>
#include <string>
#include "include/type.h"
#include "include/isa.h"
#include "load_controller.h"
#include "store_controller.h"
#include "exec_controller.h"
#include "spad_module.h"
#include "memory_arbiter.h"
#include "skew_buf_module.h"
#include "pe_array_module.h"
#include "scaler_module.h"
#include "utility/tracer.h"

#include "DRAMSys/DRAMSys.h"
#include <memory>
#include <unordered_map>

namespace sats
{
    // Owns and wires every module, and is the instruction interface: each issue function assigns an
    // id, hands the instruction to its controller, and returns the id for wait_* / is_*_complete.
    class TopModule : public sc_core::sc_module
    {
    public:
        TopModule(sc_core::sc_module_name name, const std::string &trace_path, const std::string &dramsys_config_path);

        size_t mvin(isa::MvinInstr instr);
        size_t mvout(isa::MvoutInstr instr);
        size_t gemm(isa::GemmInstr instr);
        size_t drain(isa::DrainInstr instr);

        bool has_room(isa::Op op) const;
        bool is_complete(size_t id) const;
        void wait_complete(size_t id);

        // Read or write the DRAM backing store directly, with no DRAMSys timing; for loading inputs and
        // checking results outside the simulated time.
        void dram_write_bypass(uint64_t addr, const unsigned char *data, uint64_t length);
        void dram_read_bypass(uint64_t addr, unsigned char *data, uint64_t length) const;

        // Fires whenever any controller dequeues or completes an instruction. The scoreboard waits on it
        // to reap and dispatch; the issue functions wait on the one controller they need.
        sc_core::sc_event_or_list progress;
    
    private:
        utility::Tracer tracer;
        size_t next_instr_id = 0;
        std::unordered_map<size_t, isa::Op> id_to_op;

        LoadController load_ctrl{"load_ctrl", tracer};
        StoreController store_ctrl{"store_ctrl", tracer};
        ExecController exec_ctrl{"exec_ctrl", tracer};
        MemoryArbiter mem_arbiter{"mem_arbiter", tracer};
        SpadModule spad{"spad"};
        SkewBufModule skew_a{"skew_a", tracer}, skew_b{"skew_b", tracer};
        PEArrayModule pe_array{"pe_array", tracer};
        ScalerModule scaler{"scaler", tracer};

        std::unique_ptr<DRAMSys::DRAMSys> dramsys;
        unsigned char *dram_store = nullptr;
        uint64_t dram_store_size = 0;

        // Depth-1 fifos: each stage hands one row to the next per cycle, so back-pressure is immediate.
        sc_core::sc_fifo<type::InputVector> a_to_skew_a{1}, skew_a_to_pe_array{1}, b_to_skew_b{1}, skew_b_to_pe_array{1}, scaler_to_c{1};
        sc_core::sc_fifo<type::AccVector> pe_array_to_scaler{1};

        sc_core::sc_fifo<type::ExecCmd> gemm_cmd{1};
        sc_core::sc_fifo<type::ExecCmd> drain_cmd{1};
        sc_core::sc_fifo<type::ExecCmd> skew_a_cmd{1};
        sc_core::sc_fifo<type::ExecCmd> skew_b_cmd{1};
        sc_core::sc_fifo<type::ExecCmd> scaler_cmd{1};

        void check_dram_range(uint64_t addr, uint64_t length, const std::string &ctx) const;
        void check_spad_addr_range(const type::SpadAddr &addr, size_t stride, size_t count, const std::string &ctx);
        void check_mvin(const isa::MvinInstr &instr);
        void check_mvout(const isa::MvoutInstr &instr);
        void check_gemm(const isa::GemmInstr &instr);
        void check_drain(const isa::DrainInstr &instr);

    };
}
