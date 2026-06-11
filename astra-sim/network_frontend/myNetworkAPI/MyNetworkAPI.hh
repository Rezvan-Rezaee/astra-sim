#pragma once

#include <cstdint>
#include <functional>
#include <systemc>
#include <unordered_map>

#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/Common.hh"
#include "PacketGeneratorInterface.h"
#include "SystemCScheduler.h"

// Reuse analytical frontend callback-matching helpers.
#include "common/CallbackTracker.hh"

class PacketGeneratorInterface;

namespace AstraSim {

class MyNetworkAPI : public AstraNetworkAPI {
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

    void sim_schedule(AstraSim::timespec_t delta,
                      void (*fun_ptr)(void* fun_arg),
                      void* fun_arg) override;

    AstraSim::timespec_t sim_get_time() override;

    BackendType get_backend_type() override {
        return BackendType::Custom;   // TODO: is this accurate?
    }

    // Notifies that the workload for this rank has finished.
    // Note that we have one network handler per rank.
    // Therefore, when implementing this function, the network handler must
    // find a way to concur that all ranks have finished their workloads.
    virtual void sim_notify_finished();

  private:
    struct MessageCompletionInfo {
        int tag;
        int src;
        int dst;
        uint64_t count;
        int chunk_id;
    };

    static AstraSimAnalytical::ChunkIdGenerator chunk_id_generator;
    static AstraSimAnalytical::CallbackTracker callback_tracker;

    static void process_message_arrival(void* args);
    
    PacketGeneratorInterface* packetGenerator = nullptr;
    SystemCScheduler* scheduler = nullptr;
    bool simulation_finished = false;
};

}  // namespace AstraSim
