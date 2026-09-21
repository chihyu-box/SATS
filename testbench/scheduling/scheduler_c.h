#pragma once

#include <cstddef>
#include <vector>
#include "include/config.h"
#include "scheduler_base.h"
#include "simple_spad_memory_allocator.h"

// A hand-written order for the current problem size and hardware configuration:
// 14 prefetched mvins,
// 6 reordered C tiles,
// 2 reordered gemms,
// 8 deprioritized drains,
// 7 deprioritized mvouts.

class SchedulerC : public SchedulerBase
{
public:
    SchedulerC(sc_core::sc_module_name name, sats::TopModule &top, bool print_matrices, DramLayout layout)
        : SchedulerBase(name, top, print_matrices, layout) {}

private:
    void schedule() override
    {
        size_t spad_stride = sats::config::N_BANKS;
        SimpleSpadMemoryAllocator alloc(spad_stride);

        auto wait = [&](const std::vector<size_t> &ids) { for (size_t id : ids) top.wait_complete(id); };

        // Each issue_* waits until the instructions in `after` have completed.
        auto issue_mvin_a = [&](size_t mt, size_t kt, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.mvin({.dram_src = a_dram(mt, kt), .spad_dst = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'a', mt, kt}, a_bank(mt, kt)).addr, .dims = {DIM, DIM}, .dram_stride = a_dram_stride(), .spad_stride = spad_stride});
            log.record(id, after, ScheduleLog::name("A", {mt, kt}));
            return id;
        };
        auto issue_mvin_b = [&](size_t kt, size_t nt, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.mvin({.dram_src = b_dram(kt, nt), .spad_dst = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'b', kt, nt}, b_bank(kt, nt)).addr, .dims = {DIM, DIM}, .dram_stride = b_dram_stride(), .spad_stride = spad_stride});
            log.record(id, after, ScheduleLog::name("B", {kt, nt}));
            return id;
        };
        auto issue_gemm = [&](size_t mt, size_t kt, size_t nt, size_t psum_index, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.gemm({.A_addr = alloc.lookup(SimpleSpadMemoryAllocator::TileId{'a', mt, kt}), .B_addr = alloc.lookup(SimpleSpadMemoryAllocator::TileId{'b', kt, nt}), .a_stride = spad_stride, .b_stride = spad_stride, .psum_index = psum_index});
            log.record(id, after, ScheduleLog::name("G", {mt, kt, nt}));
            return id;
        };
        auto issue_gemm_flush = [&](size_t mt, size_t nt, size_t psum_index, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.gemm({.psum_index = psum_index, .is_flush = true});
            log.record(id, after, ScheduleLog::name("F", {mt, nt}));
            return id;
        };
        auto issue_drain = [&](size_t mt, size_t nt, size_t psum_index, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.drain({.C_addr = alloc.acquire(SimpleSpadMemoryAllocator::TileId{'c', mt, nt}, c_bank(mt, nt)).addr, .c_stride = spad_stride, .psum_index = psum_index, .scale_factor = scale_factor});
            log.record(id, after, ScheduleLog::name("D", {mt, nt}));
            return id;
        };
        auto issue_mvout = [&](size_t mt, size_t nt, std::vector<size_t> after = {}) {
            wait(after);
            auto id = top.mvout({.spad_src = alloc.lookup(SimpleSpadMemoryAllocator::TileId{'c', mt, nt}), .dram_dst = c_dram(mt, nt), .dims = {DIM, DIM}, .dram_stride = c_dram_stride(), .spad_stride = spad_stride});
            log.record(id, after, ScheduleLog::name("C", {mt, nt}));
            return id;
        };

        // ---- C[0][0]
        auto a00 = issue_mvin_a(0, 0);
        auto b00 = issue_mvin_b(0, 0);
        auto a01 = issue_mvin_a(0, 1);   // prefetch mvin
        auto g000 = issue_gemm(0, 0, 0, 0, {a00, b00});
        auto b10 = issue_mvin_b(1, 0);
        auto a02 = issue_mvin_a(0, 2);   // prefetch mvin
        auto g010 = issue_gemm(0, 1, 0, 0, {a01, b10});
        auto b20 = issue_mvin_b(2, 0);
        auto a10 = issue_mvin_a(1, 0);   // prefetch mvin
        auto g020 = issue_gemm(0, 2, 0, 0, {a02, b20});
        auto a12 = issue_mvin_a(1, 2);   // prefetch mvin
        auto a11 = issue_mvin_a(1, 1);   // prefetch mvin
        auto f00 = issue_gemm_flush(0, 0, 0);

        // ---- C[1][0], reordered
        auto g100 = issue_gemm(1, 0, 0, 1, {a10, b00});
        auto b02 = issue_mvin_b(0, 2);   // prefetch mvin
        auto g110 = issue_gemm(1, 1, 0, 1, {a11, b10});
        auto b12 = issue_mvin_b(1, 2);   // prefetch mvin
        auto g120 = issue_gemm(1, 2, 0, 1, {a12, b20});
        auto f10 = issue_gemm_flush(1, 0, 1);
        auto b22 = issue_mvin_b(2, 2);   // prefetch mvin
        auto d00 = issue_drain(0, 0, 0, {g110, f00});   // deprioritize drain

        // ---- C[0][2], reordered
        auto g012 = issue_gemm(0, 1, 2, 0, {a01, b12});   // reorder gemm
        auto a21 = issue_mvin_a(2, 1, {g012});   // prefetch mvin
        auto g002 = issue_gemm(0, 0, 2, 0, {a00, b02});   // reorder gemm
        auto g022 = issue_gemm(0, 2, 2, 0, {a02, b22});
        auto f02 = issue_gemm_flush(0, 2, 0, {g022});
        auto a20 = issue_mvin_a(2, 0);   // prefetch mvin
        auto b11 = issue_mvin_b(1, 1);   // prefetch mvin
        auto d10 = issue_drain(1, 0, 1, {f10});   // deprioritize drain

        // ---- C[1][2], reordered
        auto g102 = issue_gemm(1, 0, 2, 1, {a10, b02});
        auto d02 = issue_drain(0, 2, 0, {g102, f02});   // deprioritize drain
        auto g112 = issue_gemm(1, 1, 2, 1, {a11, b12});
        auto a22 = issue_mvin_a(2, 2, {g112});   // prefetch mvin
        auto g122 = issue_gemm(1, 2, 2, 1, {a12, b22});
        auto f12 = issue_gemm_flush(1, 2, 1, {g122});
        auto b01 = issue_mvin_b(0, 1);   // prefetch mvin

        // ---- C[2][0], reordered
        auto g200 = issue_gemm(2, 0, 0, 0, {a20, b00});
        auto d12 = issue_drain(1, 2, 1, {g200, f12});   // deprioritize drain
        auto g210 = issue_gemm(2, 1, 0, 0, {a21, b10});
        auto b21 = issue_mvin_b(2, 1, {g210});   // prefetch mvin
        auto g220 = issue_gemm(2, 2, 0, 0, {a22, b20});
        auto f20 = issue_gemm_flush(2, 0, 0, {g220});

        // ---- C[0][1], reordered
        auto g001 = issue_gemm(0, 0, 1, 1, {a00, b01});
        auto d20 = issue_drain(2, 0, 0, {f20});   // deprioritize drain
        auto g011 = issue_gemm(0, 1, 1, 1, {a01, b11});
        auto g021 = issue_gemm(0, 2, 1, 1, {a02, b21});
        auto f01 = issue_gemm_flush(0, 1, 1);
        auto c10 = issue_mvout(1, 0, {g021, d10});   // deprioritize mvout
        auto c00 = issue_mvout(0, 0, {d00});   // deprioritize mvout

        // ---- C[1][1], reordered
        auto g101 = issue_gemm(1, 0, 1, 0, {a10, b01});
        auto d01 = issue_drain(0, 1, 1, {f01});   // deprioritize drain
        auto g111 = issue_gemm(1, 1, 1, 0, {a11, b11});
        auto g121 = issue_gemm(1, 2, 1, 0, {a12, b21});
        auto f11 = issue_gemm_flush(1, 1, 0);
        auto c02 = issue_mvout(0, 2, {g121, d02});   // deprioritize mvout
        auto c12 = issue_mvout(1, 2, {d12});   // deprioritize mvout

        // ---- C[2][1]
        auto g201 = issue_gemm(2, 0, 1, 1, {a20, b01});
        auto d11 = issue_drain(1, 1, 0, {g201, f11});   // deprioritize drain
        auto g211 = issue_gemm(2, 1, 1, 1, {a21, b11});
        auto c20 = issue_mvout(2, 0, {g211, d20});   // deprioritize mvout
        auto c01 = issue_mvout(0, 1, {d01});   // deprioritize mvout
        auto g221 = issue_gemm(2, 2, 1, 1, {a22, b21});
        auto f21 = issue_gemm_flush(2, 1, 1);

        // ---- C[2][2]
        auto g202 = issue_gemm(2, 0, 2, 0, {a20, b02});
        auto g212 = issue_gemm(2, 1, 2, 0, {a21, b12});
        auto g222 = issue_gemm(2, 2, 2, 0, {a22, b22});
        auto c11 = issue_mvout(1, 1, {g222, d11});   // deprioritize mvout
        auto f22 = issue_gemm_flush(2, 2, 0);
        auto d21 = issue_drain(2, 1, 1, {f21});   // deprioritize drain
        auto c21 = issue_mvout(2, 1, {d21});
        auto d22 = issue_drain(2, 2, 0, {f22});
        auto c22 = issue_mvout(2, 2, {d22});

        wait({c10, c00, c02, c12, c20, c01, c11, c21, c22});   // validate_gemm() reads C as soon as schedule() returns
    }
};
