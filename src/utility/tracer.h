#pragma once

#include <fstream>
#include <iomanip>
#include <string>
#include <systemc>
#include "include/config.h"

namespace sats::utility
{
    class Tracer
    {
    public:
        // An empty path disables tracing: log() returns without writing.
        explicit Tracer(const std::string &path)
        {
            if (path.empty())
                return;

            out.open(path);
            out << "time,engine,id,event,detail\n";

            log("config", 0, "params",
                "DIM=" + std::to_string(config::DIM) +
                    ",N_BANKS=" + std::to_string(config::N_BANKS) +
                    ",N_ROWS_PER_BANK=" + std::to_string(config::N_ROWS_PER_BANK) +
                    ",BYTES_PER_BANK_ROW=" + std::to_string(config::BYTES_PER_BANK_ROW) +
                    ",INSTR_QUEUE_SIZE=" + std::to_string(config::INSTR_QUEUE_SIZE) +
                    ",SPAD_CREDIT_SIZE=" + std::to_string(config::SPAD_CREDIT_SIZE) +
                    ",LOAD_N_DATA_BUF=" + std::to_string(config::LOAD_N_DATA_BUF) +
                    ",STORE_N_DATA_BUF=" + std::to_string(config::STORE_N_DATA_BUF));
        }

        void log(const std::string &engine, size_t id, const std::string &event,
                 const std::string &detail = "")
        {
            if (!out.is_open())
                return;
            out << std::setprecision(15) << sc_core::sc_time_stamp().to_seconds() * 1e9 << "ns,"
                << engine << ',' << id << ',' << event << ',' << detail << '\n';
        }

    private:
        std::ofstream out;
    };
}
