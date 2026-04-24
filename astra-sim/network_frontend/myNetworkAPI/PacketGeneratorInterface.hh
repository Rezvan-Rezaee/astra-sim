#pragma once

#include "basics.h"

#include "astra-sim/system/Common.hh"
#include <cstdint>

struct messageRecord {
    double time = 0.0;
    addr_t src = 0;
    addr_t dst = 0;
    std::string messageId;
    size_t messageSizeBytes = 0;
    // int establish = 0;
    size_t tag = 0;
    size_t returnToRank = 0;
};

// timespec_t systemcTimeToTimespec(const sc_core::sc_time& t)
// {
//     timespec_t ts;

//     ts.time_res = NS;
//     ts.time_val = t.to_seconds() * 1e9L;

//     return ts;
// }

class PacketGeneratorInterface {
  public:
    virtual ~PacketGeneratorInterface() = default;

    /**
     * This function needs to receive information about a message being sent,
     * and inject it into the packet generator. functionality: 1-set a time
     * stamp for the message, 2- create a message ID, 3- finalize messageRecord
     * and inject the message into the packet generator
     */
    virtual void inject_message(size_t messageId,
                                addr_t src,
                                addr_t dst,
                                size_t messageSizeBytes,
                                size_t tag,
                                size_t returnToRank) = 0;

    // virtual timespec_t get_current_time() = 0;
    virtual uint64_t generate_message_id();

  private:
    uint64_t nextMessageId = 0;

    uint64_t generate_message_id();
};

uint64_t PacketGeneratorInterface::generate_message_id() {
    return nextMessageId++;
}
