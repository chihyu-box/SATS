#pragma once

#include <systemc>
#include <tlm>

namespace sats::utility
{
    class DummyMemoryManager : public tlm::tlm_mm_interface
    {
    public:
        void free(tlm::tlm_generic_payload *payload) override {}
    };
}
