#pragma once

#include <cstdint>
#include <ostream>
#include "type.h"

namespace sats::isa
{
    enum class Op
    {
        Mvin,
        Mvout,
        Gemm,
        Drain
    };

    // Copy a tile from DRAM into the scratchpad, one row per transfer:
    // row i goes from dram_src + i*dram_stride to spad_dst + i*spad_stride.
    // Only dims.rows sets the transfer count; every row is a full DIM-wide row.
    struct MvinInstr
    {
        uint64_t dram_src;
        type::SpadAddr spad_dst;
        type::MatrixDims dims;
        size_t dram_stride;
        size_t spad_stride;
        size_t instr_id;
        friend std::ostream &operator<<(std::ostream &os, const MvinInstr &mi)  { return os; }
    };

    // Copy a tile from the scratchpad back to DRAM -- the reverse of MvinInstr:
    // row i goes from spad_src + i*spad_stride to dram_dst + i*dram_stride.
    struct MvoutInstr
    {
        type::SpadAddr spad_src;
        uint64_t dram_dst;
        type::MatrixDims dims;
        size_t dram_stride;
        size_t spad_stride;
        size_t instr_id;
        friend std::ostream &operator<<(std::ostream &os, const MvoutInstr &mi) { return os; }
    };

    // Multiply one A tile by one B tile (row i = A_addr + i*a_stride, B_addr + i*b_stride) and add the
    // result into psum[psum_index]; issuing several on the same buffer accumulates them, i.e. a K-loop.
    // is_flush ends such a chain: zeros are pushed in until the array's last partial sums have reached
    // psum. A chain must be flushed before that buffer can be drained.
    struct GemmInstr
    {
        type::SpadAddr A_addr;
        type::SpadAddr B_addr;
        size_t a_stride;
        size_t b_stride;
        size_t psum_index;
        bool is_flush; // when set, only psum_index is used -- the A/B fields above are ignored
        size_t instr_id;
        friend std::ostream &operator<<(std::ostream &os, const GemmInstr &gi)  { return os; }
    };

    // Write a finished psum[psum_index] out to the scratchpad: DIM rows are multiplied by the
    // fixed-point scale_factor, rounded and saturated to input width, and stored at
    // C_addr + i*c_stride, leaving the buffer cleared for the next chain.
    // Only valid once that chain has been closed by a flush gemm.
    struct DrainInstr
    {
        type::SpadAddr C_addr;
        size_t c_stride;
        size_t psum_index;
        type::ScaleFactor scale_factor;
        size_t instr_id;
        friend std::ostream &operator<<(std::ostream &os, const DrainInstr &di) { return os; }
    };
}
