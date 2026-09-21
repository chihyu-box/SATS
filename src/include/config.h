#pragma once

#include <cstddef>
#include <cstdint>
#include <systemc>

namespace sats::config
{
    using inputType = int8_t;
    using accType = int32_t;

    constexpr size_t DIM = 32;

    constexpr size_t N_BANKS = 4;
    constexpr size_t N_ROWS_PER_BANK = 8192;
    constexpr size_t BYTES_PER_BANK_ROW = DIM * sizeof(inputType);

    constexpr double CLOCK_NS = 1.0;
    inline sc_core::sc_time CYCLE_TIME = sc_core::sc_time(CLOCK_NS, sc_core::SC_NS);
    inline sc_core::sc_time BANK_ACCESS_TIME = CYCLE_TIME;
    inline sc_core::sc_time PE_MAC_TIME = CYCLE_TIME;
    inline sc_core::sc_time PE_DRAIN_TIME = CYCLE_TIME;
    inline sc_core::sc_time SCALER_SCALE_TIME = CYCLE_TIME;
    inline sc_core::sc_time SKEW_BUF_SHIFT_TIME = CYCLE_TIME;

    constexpr size_t INSTR_QUEUE_SIZE = 4;
    constexpr size_t SPAD_CREDIT_SIZE = N_BANKS;   // scratchpad requests a controller may have outstanding
    constexpr size_t LOAD_N_DATA_BUF = 16;         // mvin rows in flight between DRAM and the scratchpad
    constexpr size_t STORE_N_DATA_BUF = 8;         // mvout rows in flight between the scratchpad and DRAM

    constexpr const char* DRAMSYS_CONFIG_PATH = "dram_config/hbm2-example.json";

    static_assert(DIM >= 2, "DIM must be at least 2");
    static_assert((DIM & (DIM - 1)) == 0, "DIM must be a power of 2");
    static_assert((N_BANKS & (N_BANKS - 1)) == 0, "N_BANKS must be a power of 2");
    static_assert((N_ROWS_PER_BANK & (N_ROWS_PER_BANK - 1)) == 0, "N_ROWS_PER_BANK must be a power of 2");
    static_assert(N_ROWS_PER_BANK % DIM == 0, "N_ROWS_PER_BANK must be a multiple of DIM");
}
