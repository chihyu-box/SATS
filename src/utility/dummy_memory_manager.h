#pragma once

#include <systemc>
#include <tlm>

namespace sats::utility
{
    // Used for integrating with DRAMSys
    class DummyMemoryManager : public tlm::tlm_mm_interface
    {
    public:
        void free(tlm::tlm_generic_payload *payload) override {}
    };
}
