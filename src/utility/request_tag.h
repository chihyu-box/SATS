#pragma once

#include <cstddef>
#include <tlm>
#include "include/type.h"

namespace sats::utility
{
    class RequestTag : public tlm::tlm_extension<RequestTag>
    {
    public:
        size_t instr_id = 0;
        size_t row = 0;
        size_t buf = 0;
        size_t credit = 0;
        type::SpadAddr spad_dst;

        tlm_extension_base *clone() const override { return new RequestTag(*this); }
        void copy_from(const tlm_extension_base &ext) override
        {
            const auto &other = static_cast<const RequestTag &>(ext);
            instr_id = other.instr_id;
            row = other.row;
            buf = other.buf;
            credit = other.credit;
            spad_dst = other.spad_dst;
        }
    };

    inline RequestTag &tag_of(tlm::tlm_generic_payload &payload)
    {
        return *payload.get_extension<RequestTag>();
    }
}
