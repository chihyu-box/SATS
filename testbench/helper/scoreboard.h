#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <systemc>
#include <vector>
#include "include/config.h"
#include "include/type.h"
#include "module/top_module.h"
#include "common.h"

// Takes instructions in program order and dispatches each one once no earlier instruction still
// has a RAW, WAW or WAR conflict with it.
class Scoreboard : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(Scoreboard);
    Scoreboard(sc_core::sc_module_name name, sats::TopModule &top, ScheduleLog &log, size_t queue_depth)
        : sc_core::sc_module(name), top(top), log(log), queue_depth(queue_depth)
    {
        SC_THREAD(dispatch_loop);
    }

    void mvin(sats::isa::MvinInstr instr, std::string name)
    {
        issue({.kind = sats::isa::Op::Mvin, .name = std::move(name),
               .accesses = {{spad(instr.spad_dst, instr.spad_stride, instr.dims.rows), true},
                            {dram(instr.dram_src, instr.dram_stride, instr.dims.rows), false}},
               .dispatch = [instr, this] { return top.mvin(instr); }});
    }

    void mvout(sats::isa::MvoutInstr instr, std::string name)
    {
        issue({.kind = sats::isa::Op::Mvout, .name = std::move(name),
               .accesses = {{spad(instr.spad_src, instr.spad_stride, instr.dims.rows), false},
                            {dram(instr.dram_dst, instr.dram_stride, instr.dims.rows), true}},
               .dispatch = [instr, this] { return top.mvout(instr); }});
    }

    void gemm(sats::isa::GemmInstr instr, std::string name)
    {
        if (instr.is_flush)
            issue({.kind = sats::isa::Op::Gemm, .name = std::move(name),
                   .accesses = {{psum(instr.psum_index), true},
                                {array(), true}},
                   .dispatch = [instr, this] { return top.gemm(instr); }});
        else
            issue({.kind = sats::isa::Op::Gemm, .name = std::move(name),
                   .accesses = {{spad(instr.A_addr, instr.a_stride, sats::config::DIM), false},
                                {spad(instr.B_addr, instr.b_stride, sats::config::DIM), false},
                                {psum(instr.psum_index), true},
                                {array(), true}},
                   .dispatch = [instr, this] { return top.gemm(instr); }});
    }

    void drain(sats::isa::DrainInstr instr, std::string name)
    {
        issue({.kind = sats::isa::Op::Drain, .name = std::move(name),
               .accesses = {{spad(instr.C_addr, instr.c_stride, sats::config::DIM), true},
                            {psum(instr.psum_index), false}},
               .dispatch = [instr, this] { return top.drain(instr); }});
    }

    // Returns once every issued instruction has completed.
    void fence()
    {
        reap_and_dispatch();
        while (!pending.empty() || !in_flight.empty())
        {
            sc_core::wait(top.progress);
            reap_and_dispatch();
        }
    }

private:
    // Every operand maps into one pseudo address space so that overlap() can compare any two:
    // scratchpad rows keep their flat address, DRAM addresses are offset by kDramBase, the two psum
    // buffers are kPsum and kPsum + 1, and the PE array is kArray.
    static constexpr uint64_t kDramBase = uint64_t{1} << 40;
    static constexpr uint64_t kArray = std::numeric_limits<uint64_t>::max();
    static constexpr uint64_t kPsum = kArray - 2;

    // The addresses lo, lo + stride, ..., lo + (count - 1) * stride.
    struct Range
    {
        uint64_t lo, stride, count;
    };

    struct Access
    {
        Range range;
        bool is_write;
    };

    struct PendingOp
    {
        sats::isa::Op kind;
        std::string name;
        std::vector<Access> accesses;
        std::function<size_t()> dispatch;
    };

    struct InFlightOp
    {
        size_t id;
        std::vector<Access> accesses;
    };

    static Range spad(sats::type::SpadAddr spad_addr, size_t spad_stride, size_t rows)
    {
        assert(spad_stride > 0);
        return {spad_addr.to_flat(), spad_stride, rows};
    }

    static Range dram(uint64_t dram_addr, size_t dram_stride, size_t rows)
    {
        assert(dram_stride > 0);
        return {kDramBase + dram_addr, dram_stride, rows};
    }

    static Range psum(size_t psum_index)
    {
        assert(psum_index < 2);   // a larger index would land on kArray
        return {kPsum + psum_index, 1, 1};
    }

    static Range array()
    {
        return {kArray, 1, 1};
    }

    static bool contains(const Range &r, uint64_t x)
    {
        if (x < r.lo)
            return false;
        uint64_t delta = x - r.lo;
        return delta % r.stride == 0 && delta / r.stride < r.count;
    }

    static bool overlap(const Range &a, const Range &b)
    {
        const Range &few = a.count <= b.count ? a : b;
        const Range &many = a.count <= b.count ? b : a;
        for (uint64_t i = 0; i < few.count; ++i)
            if (contains(many, few.lo + i * few.stride))
                return true;
        return false;
    }

    static bool ops_conflict(const std::vector<Access> &a, const std::vector<Access> &b)
    {
        for (const auto &x : a)
            for (const auto &y : b)
                if ((x.is_write || y.is_write) && overlap(x.range, y.range))
                    return true;
        return false;
    }

    bool blocked(const PendingOp &op, size_t index) const
    {
        for (const auto &flight : in_flight)
            if (ops_conflict(op.accesses, flight.accesses))
                return true;
        for (size_t ahead = 0; ahead < index; ++ahead)
            if (ops_conflict(op.accesses, pending[ahead].accesses))
                return true;
        return false;
    }

    void issue(PendingOp op)
    {
        while (pending.size() >= queue_depth)
        {
            sc_core::wait(top.progress);
            reap_and_dispatch();
        }

        pending.push_back(std::move(op));
        try_dispatch();
    }
    
    void try_dispatch()
    {
        for (size_t i = 0; i < pending.size();)
        {
            PendingOp &op = pending[i];
            if (blocked(op, i) || !top.has_room(op.kind))
            {
                ++i;
                continue;
            }

            size_t id = op.dispatch();
            log.record(id, {}, op.name);
            in_flight.push_back({id, op.accesses});
            pending.erase(pending.begin() + static_cast<long>(i));
        }
    }

    void reap_and_dispatch()
    {
        std::erase_if(in_flight, [this](const InFlightOp &f) { return top.is_complete(f.id); });
        try_dispatch();
    }

    void dispatch_loop()
    {
        while (true)
        {
            sc_core::wait(top.progress);
            reap_and_dispatch();
        }
    }

    sats::TopModule &top;
    ScheduleLog &log;
    size_t queue_depth;

    std::vector<PendingOp> pending;
    std::vector<InFlightOp> in_flight;
};
