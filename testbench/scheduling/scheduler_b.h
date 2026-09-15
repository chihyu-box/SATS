#pragma once

#include <cstddef>
#include "include/config.h"
#include "scheduler_base.h"
#include "scoreboard.h"
#include "simple_spad_memory_allocator.h"

// Program order goes to the scoreboard, which overlaps whatever the operands allow; nothing is
// waited for by hand.
class SchedulerB : public SchedulerBase
{
public:
    SchedulerB(sc_core::sc_module_name name, sats::TopModule &top, bool print_matrices, DramLayout layout)
        : SchedulerBase(name, top, print_matrices, layout), sb("scoreboard", top, log, 8) {}

private:
    void schedule() override
    {
        size_t spad_stride = sats::config::N_BANKS;
        SimpleSpadMemoryAllocator alloc(spad_stride);

        size_t psum_index = 0;

        for (size_t mt = 0; mt < MT; ++mt)
        {
            for (size_t nt = 0; nt < NT; ++nt)
            {
                for (size_t kt = 0; kt < KT; ++kt)
                {
                    auto a = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'a', mt, kt}, a_bank(mt, kt));
                    auto b = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'b', kt, nt}, b_bank(kt, nt));

                    if (!a.hit)
                        sb.mvin({.dram_src = a_dram(mt, kt), .spad_dst = a.addr, .dims = {DIM, DIM}, .dram_stride = a_dram_stride(), .spad_stride = spad_stride}, ScheduleLog::name("A", {mt, kt}));

                    if (!b.hit)
                        sb.mvin({.dram_src = b_dram(kt, nt), .spad_dst = b.addr, .dims = {DIM, DIM}, .dram_stride = b_dram_stride(), .spad_stride = spad_stride}, ScheduleLog::name("B", {kt, nt}));

                    sb.gemm({.A_addr = a.addr, .B_addr = b.addr, .a_stride = spad_stride, .b_stride = spad_stride, .psum_index = psum_index}, ScheduleLog::name("G", {mt, kt, nt}));

                    if (kt == KT - 1)
                    {
                        sb.gemm({.psum_index = psum_index, .is_flush = true}, ScheduleLog::name("F", {mt, nt}));

                        auto c = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'c', mt, nt}, c_bank(mt, nt));
                        sb.drain({.C_addr = c.addr, .c_stride = spad_stride, .psum_index = psum_index}, ScheduleLog::name("D", {mt, nt}));
                        
                        sb.mvout({.spad_src = c.addr, .dram_dst = c_dram(mt, nt), .dims = {DIM, DIM}, .dram_stride = c_dram_stride(), .spad_stride = spad_stride}, ScheduleLog::name("C", {mt, nt}));
                        
                        psum_index ^= 1;
                    }
                }
            }
        }

        sb.fence();
    }

    Scoreboard sb;
};
