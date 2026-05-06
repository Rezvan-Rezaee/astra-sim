#include "MyNetworkAPI.hh"

using namespace AstraSim;

MyNetworkAPI::MyNetworkAPI(int rank,
                           PacketGeneratorInterface* pg,
                           SystemCScheduler* sched)
    : AstraNetworkAPI(rank),
      packetGenerator(pg),
      scheduler(sched) {}

int MyNetworkAPI::sim_send(void* buffer,
                           uint64_t count,
                           int type,
                           int dst,
                           int tag,
                           sim_request* request,
                           void (*msg_handler)(void* fun_arg),
                           void* fun_arg) {
    size_t id = packetGenerator->generate_message_id();

    packetGenerator->inject_message(id, rank, dst, tag, count, type,
                                    msg_handler, fun_arg);

    return 0;
}

int MyNetworkAPI::sim_recv(void* buffer,
                           uint64_t count,
                           int type,
                           int src,
                           int tag,
                           sim_request* request,
                           void (*msg_handler)(void* callbackArg),
                           void* fun_arg) {
    size_t id = packetGenerator->generate_message_id();

    packetGenerator->inject_message(id, src, rank, tag, count, type,
                                    msg_handler, fun_arg);

    return 0;
}

void MyNetworkAPI::sim_schedule(AstraSim::timespec_t delta,
                                void (*fun_ptr)(void* fun_arg),
                                void* fun_arg) {

    ASSERT_PRINT(scheduler != nullptr, "MyNetworkAPI: scheduler is null.");

    scheduler->schedule(delta, fun_ptr, fun_arg);
}

AstraSim::timespec_t MyNetworkAPI::sim_get_time() {
    return systemCTimeToTimespec(packetGenerator->get_current_time());
}

void MyNetworkAPI::sim_notify_finished() {
    packetGenerator->notify_finished(rank);
    return;  // TODO: implement this function when we have a better idea of how
             // to determine when all ranks are finished.
}
