#pragma once

#include <cstdint>
#include <functional>
#include <systemc>
#include <unordered_map>

#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/system/Common.hh"
#include "PacketGeneratorInterface.h"
#include "SystemCScheduler.h"

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
    PacketGeneratorInterface* packetGenerator = nullptr;
    SystemCScheduler* scheduler = nullptr;
    bool simulation_finished = false;
};

}  // namespace AstraSim
