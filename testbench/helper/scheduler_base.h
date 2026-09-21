#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <systemc>
#include "include/config.h"
#include "module/top_module.h"
#include "common.h"

// Every scheduler runs the same GEMM in its own instruction order and reports the simulated time.
class SchedulerBase : public sc_core::sc_module
{
public:
    SC_HAS_PROCESS(SchedulerBase);
    SchedulerBase(sc_core::sc_module_name name, sats::TopModule &top, bool print_matrices, DramLayout layout)
        : sc_core::sc_module(name), top(top), log(std::string("deps_") + this->name() + ".csv"), print_matrices(print_matrices), layout(layout)
    {
        SC_THREAD(run_thread);
    }

protected:
    // Issues every instruction and returns once all of them have completed.
    virtual void schedule() = 0;

    static constexpr size_t DIM = sats::config::DIM;
    static constexpr size_t elem_size = sizeof(sats::config::inputType);

    // The test computes C[M][N] = A[M][K] x B[K][N] with every matrix padded to whole tiles and A
    // stored in DRAM transposed.
    static constexpr size_t M = 65;
    static constexpr size_t K = 66;
    static constexpr size_t N = 67;
    static constexpr size_t PM = pad_to_dim(M);
    static constexpr size_t PK = pad_to_dim(K);
    static constexpr size_t PN = pad_to_dim(N);
    static constexpr size_t MT = PM / DIM;
    static constexpr size_t KT = PK / DIM;
    static constexpr size_t NT = PN / DIM;
    static inline const Matrix A = Matrix(M, K, 1);
    static inline const Matrix AT = A.transpose();
    static inline const Matrix B = Matrix(K, N, 2);
    static constexpr sats::type::ScaleFactor scale_factor = quantize_scale(1.0 / 1400);

    // A tiles use the even banks, B tiles the odd banks, and C tiles every bank in turn.
    static_assert(sats::config::N_BANKS % 2 == 0 && sats::config::N_BANKS >= 2, "the even/odd bank split needs an even bank count of at least two");
    static size_t a_bank(size_t mt, size_t kt) { return (mt * KT + kt) % (sats::config::N_BANKS / 2) * 2; }
    static size_t b_bank(size_t kt, size_t nt) { return (kt * NT + nt) % (sats::config::N_BANKS / 2) * 2 + 1; }
    static size_t c_bank(size_t mt, size_t nt) { return (mt * NT + nt) % sats::config::N_BANKS; }

    // The DRAM address and row stride of a tile: A and B follow the layout, and C is always
    // row-major.
    uint64_t a_dram(size_t mt, size_t kt) const { return a_base + (tiled() ? (kt * MT + mt) * DIM * DIM : kt * DIM * PM + mt * DIM) * elem_size; }
    uint64_t b_dram(size_t kt, size_t nt) const { return b_base + (tiled() ? (kt * NT + nt) * DIM * DIM : kt * DIM * PN + nt * DIM) * elem_size; }
    uint64_t c_dram(size_t mt, size_t nt) const { return c_base + (mt * DIM * PN + nt * DIM) * elem_size; } 
    size_t a_dram_stride() const { return (tiled() ? DIM : PM) * elem_size; }
    size_t b_dram_stride() const { return (tiled() ? DIM : PN) * elem_size; }
    size_t c_dram_stride() const { return PN * elem_size; }

    sats::TopModule &top;
    ScheduleLog log;

private:
    bool tiled() const { return layout == DramLayout::TileMajor; }

    bool print_matrices;
    DramLayout layout;
    // DRAM base of AT, of B, and of C.
    uint64_t a_base;
    uint64_t b_base;
    uint64_t c_base;

    void run_thread()
    {
        sc_core::sc_time start = sc_core::sc_time_stamp();

        if (print_matrices)
        {
            A.print("A");
            B.print("B");
        }

        a_base = 0;
        b_base = a_base + dram_initialize(top, AT, a_base, layout);
        c_base = b_base + dram_initialize(top, B, b_base, layout);

        schedule();

        bool ok = validate_gemm(top, c_base, A, B, scale_factor, print_matrices);

        sc_core::sc_time elapsed = sc_core::sc_time_stamp() - start;
        std::cout << "[" << name() << "] " << (ok ? "PASS" : "FAIL") << " sim time: " << elapsed << std::endl;

        sc_core::sc_stop();
    }
};
