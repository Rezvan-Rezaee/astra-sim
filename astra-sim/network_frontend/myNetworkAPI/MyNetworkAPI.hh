#pragma once

#include <cstdint>
#include <functional>
#include <systemc>
#include <unordered_map>

#include "SystemCScheduler.hh"
#include "TimeConversion.hh"
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/Common.hh"

class PacketGeneratorInterface;

namespace AstraSim {

class MyNetworkAPI : public AstraNetworkAPI, public sc_core::sc_module {
  public:
    MyNetworkAPI(int rank,
                 PacketGeneratorInterface* pg,
                 SystemCScheduler* sched);

    ~MyNetworkAPI() override = default;

    int sim_send(void* buffer,
                 uint64_t count,
                 int type,
                 int dst,
                 int tag,
                 sim_request* request,
                 void (*msg_handler)(void* fun_arg),
                 void* fun_arg) override;

    int sim_recv(void* buffer,
                 uint64_t count,
                 int type,
                 int src,
                 int tag,
                 sim_request* request,
                 void (*msg_handler)(void* fun_arg),
                 void* fun_arg) override;

    void sim_schedule(timespec_t delta,
                      void (*fun_ptr)(void* fun_arg),
                      void* fun_arg) override;

    timespec_t sim_get_time()
        override;  // TODO: define the time unit and conversion

    BackendType get_backend_type() override {
        return BackendType::Custom;
    }

    void notify_message_complete(uint64_t messageId);

    timespec_t sim_get_time() {
        return packetGenerator->get_current_time();
    };

    // Notifies that the workload for this rank has finished.
    // Note that we have one network handler per rank.
    // Therefore, when implementing this function, the network handler must
    // find a way to concur that all ranks have finished their workloads.
    virtual void sim_notify_finished() {
        return;
    }

  private:
    struct PendingMessage {
        uint64_t id;

        int src;
        int dst;
        int tag;
        uint64_t count;
        int type;

        sim_request* request;

        void (*callback)(void* callbackArg);
        void* callbackArg;
    };

    struct ScheduledEvent {
        void (*fun_ptr)(void* fun_arg);
        void* fun_arg;
        sc_time scheduledWaitTime;
    };  // TODO: how many of these do we need to support at the same time? If we
        // only need to support one scheduled event at a time, we can store the
        // ScheduledEvent directly in MyNetworkAPI instead of using a queue.

    std::unordered_map<uint64_t, PendingMessage> pendingMessages;
    PacketGeneratorInterface* packetGenerator;
    uint64_t convert_count_to_bytes(uint64_t count, int type);
    sc_event scheduledEvent;
    ScheduledEvent m_scheduledEvent;
    SystemCScheduler* scheduler = nullptr;
};

}  // namespace AstraSim
