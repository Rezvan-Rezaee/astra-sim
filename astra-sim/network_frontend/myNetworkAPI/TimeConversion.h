#pragma once

#include <cmath>
#include <systemc>
#include "astra-sim/system/Common.hh"

inline sc_core::sc_time timespecToSystemCTime(const AstraSim::timespec_t& t)
{
    // std::cout << "Converting timespec_t to sc_time: time_res = " << t.time_res << ", time_val = " << t.time_val << std::endl;
    switch (t.time_res)
    {
        case AstraSim::SE:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_SEC);

        case AstraSim::MS:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_MS);

        case AstraSim::US:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_US);

        case AstraSim::NS:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_NS);

        case AstraSim::FS:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_FS);

        default:
            return sc_core::sc_time(static_cast<double>(t.time_val), sc_core::SC_NS);
    }
}

inline AstraSim::timespec_t systemCTimeToTimespec(const sc_core::sc_time& t)
{
    AstraSim::timespec_t ts;
    ts.time_res = AstraSim::NS;
    ts.time_val = std::round(t.to_seconds() * 1e9L);
    return ts;
}