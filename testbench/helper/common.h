#pragma once

#include "module/top_module.h"
#include "logic/scaler_logic.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

// Records what the trace cannot show: which instructions the scheduler chose to wait for, and a
// name for each instruction so the viewers can label it.
class ScheduleLog
{
public:
    explicit ScheduleLog(const std::string &path) : out(path)
    {
        out << "id,deps,name\n";
    }

    static std::string name(const char *tag, std::initializer_list<size_t> coords)
    {
        std::string s = tag;
        for (size_t c : coords)
            s += std::to_string(c);
        return s;
    }

    void record(size_t instr_id, const std::vector<size_t> &deps, const std::string &name)
    {
        out << instr_id << ",";
        for (size_t i = 0; i < deps.size(); ++i)
            out << (i ? ";" : "") << deps[i];
        out << "," << name << "\n";
    }

private:
    std::ofstream out;
};

constexpr size_t pad_to_dim(size_t x)
{
    return ((x + sats::config::DIM - 1) / sats::config::DIM) * sats::config::DIM;
}

// A rows x cols matrix, row-major and padded to a multiple of DIM in both directions: the real
// contents sit in the top-left corner and the padding stays zero.
class Matrix
{
public:
    const size_t rows, cols;
    const size_t padded_rows, padded_cols;

    Matrix(size_t rows, size_t cols)
        : rows(rows), cols(cols), padded_rows(pad_to_dim(rows)), padded_cols(pad_to_dim(cols)),
          data(padded_rows * padded_cols) {}

    // Fills the real contents with values drawn uniformly over inputType.
    Matrix(size_t rows, size_t cols, uint64_t seed) : Matrix(rows, cols)
    {
        constexpr uint64_t range = uint64_t{std::numeric_limits<sats::config::inputType>::max()} + 1;
        std::mt19937_64 rng(seed);
        for (size_t r = 0; r < rows; ++r)
            for (size_t c = 0; c < cols; ++c)
                at(r, c) = static_cast<sats::config::inputType>(rng() % range);
    }

    sats::config::inputType &at(size_t r, size_t c) { return data[r * padded_cols + c]; }
    sats::config::inputType at(size_t r, size_t c) const { return data[r * padded_cols + c]; }

    Matrix transpose() const
    {
        Matrix t(cols, rows);
        for (size_t r = 0; r < rows; ++r)
            for (size_t c = 0; c < cols; ++c)
                t.at(c, r) = at(r, c);
        return t;
    }

    // The padded storage as bytes, for reading a matrix back from DRAM.
    unsigned char *raw() { return reinterpret_cast<unsigned char *>(data.data()); }
    size_t bytes() const { return data.size() * sizeof(sats::config::inputType); }

    // Prints the real contents without the padding.
    void print(const std::string &label) const
    {
        std::cout << label << ":" << std::endl;
        for (size_t r = 0; r < rows; ++r)
        {
            for (size_t c = 0; c < cols; ++c)
                std::cout << std::setw(4) << static_cast<int>(at(r, c));
            std::cout << std::endl;
        }
    }

private:
    std::vector<sats::config::inputType> data;
};

// How a padded matrix sits in DRAM. RowMajor: one matrix row after another. TileMajor: one
// DIM x DIM tile after another, tiles taken left to right then top to bottom, each tile's rows
// contiguous.
enum class DramLayout
{
    RowMajor,
    TileMajor,
};

// Writes the padded m to DRAM at base in the given layout and returns the byte count written.
inline uint64_t dram_initialize(sats::TopModule &top, const Matrix &m, uint64_t base, DramLayout layout)
{
    std::vector<sats::config::inputType> data;
    data.reserve(m.padded_rows * m.padded_cols);

    if (layout == DramLayout::RowMajor)
    {
        for (size_t r = 0; r < m.padded_rows; ++r)
            for (size_t c = 0; c < m.padded_cols; ++c)
                data.push_back(m.at(r, c));
    }
    else
    {
        for (size_t tr = 0; tr < m.padded_rows / sats::config::DIM; ++tr)
            for (size_t tc = 0; tc < m.padded_cols / sats::config::DIM; ++tc)
                for (size_t r = 0; r < sats::config::DIM; ++r)
                    for (size_t c = 0; c < sats::config::DIM; ++c)
                        data.push_back(m.at(tr * sats::config::DIM + r, tc * sats::config::DIM + c));
    }

    uint64_t bytes = data.size() * sizeof(sats::config::inputType);
    top.dram_write_bypass(base, reinterpret_cast<const unsigned char *>(data.data()), bytes);
    return bytes;
}

// Reads C back from DRAM and compares its real contents with the product A * B computed here in
// software.
inline bool validate_gemm(sats::TopModule &top, uint64_t C_dram_addr,
                          const Matrix &A, const Matrix &B, bool print_matrices)
{
    assert(A.cols == B.rows && "A's columns must match B's rows");

    Matrix C_hardware(A.rows, B.cols);
    Matrix C_expected(A.rows, B.cols);
    bool ok = true;
    size_t saturated = 0;

    top.dram_read_bypass(C_dram_addr, C_hardware.raw(), C_hardware.bytes());

    for (size_t m = 0; m < A.rows; ++m)
    {
        for (size_t n = 0; n < B.cols; ++n)
        {
            sats::config::accType acc = 0;
            for (size_t k = 0; k < A.cols; ++k)
                acc += static_cast<sats::config::accType>(A.at(m, k)) * static_cast<sats::config::accType>(B.at(k, n));
            C_expected.at(m, n) = sats::logic::ScalerLogic::scale(acc);
            saturated += C_expected.at(m, n) == std::numeric_limits<sats::config::inputType>::max();

            if (C_expected.at(m, n) != C_hardware.at(m, n))
                ok = false;
        }
    }

    // The scaler saturates at the maximum of inputType, so a wrong sum that is too large still comes
    // out at that maximum and the check passes. Set SCALE_FACTOR so that C spreads over inputType
    // without reaching it.
    if (saturated > 0)
        std::cerr << "warning: " << saturated << " of " << A.rows * B.cols << " expected C entries saturate; "
                  << "validation is blind to errors there" << std::endl;

    if (print_matrices)
    {
        C_expected.print("C (expected)");
        C_hardware.print("C (hardware)");
    }

    return ok;
}
