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

    packetGenerator->inject_message(id, rank, dst, tag, count, type, request,
                                    msg_handler, fun_arg, rank);

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

    packetGenerator->inject_message(id, src, rank, tag, count, type, request,
                                    msg_handler, fun_arg, rank);

    return 0;
}

void MyNetworkAPI::sim_schedule(timespec_t delta,
                                void (*fun_ptr)(void* fun_arg),
                                void* fun_arg) {

    ASSERT_PRINT(scheduler != nullptr, "MyNetworkAPI: scheduler is null.");

    scheduler->schedule(delta, fun_ptr, fun_arg);
}
