#include "MyNetworkAPI.hh"

using namespace AstraSim;

const int COUNT_TO_BYTES = 4;  // TODO: Placeholder, should be based on type

MyNetworkAPI::MyNetworkAPI(int rank,
                           PacketGeneratorInterface* pg,
                           SystemCScheduler* sched)
    : AstraNetworkAPI(rank),
      packetGenerator(pg),
      scheduler(sched)
{
}

uint64_t MyNetworkAPI::convert_count_to_bytes(uint64_t count, int type) {
    return count * COUNT_TO_BYTES;
}

int MyNetworkAPI::sim_send(void* buffer,
                           uint64_t count,
                           int type,
                           int dst,
                           int tag,
                           sim_request* request,
                           void (*msg_handler)(void* fun_arg),
                           void* fun_arg) {
    uint64_t id = packetGenerator->generate_message_id();

    PendingMessage msg;
    msg.id = id;
    msg.src = rank;
    msg.dst = dst;
    msg.tag = tag;
    msg.count = count;
    msg.type = type;
    msg.request = request;
    msg.callback = msg_handler;
    msg.callbackArg = fun_arg;

    pendingMessages[id] = msg;

    packetGenerator->inject_message(id, rank, dst,
                                    convert_count_to_bytes(count, type), tag,
                                    rank);  // TODO: we might need a conversion
                                            // function uint64_t, size_t, addr_t

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
    uint64_t id = packetGenerator->generate_message_id();

    PendingMessage msg;
    msg.id = id;
    msg.src = src;
    msg.dst = rank;
    msg.tag = tag;
    msg.count = count;
    msg.type = type;
    msg.request = request;
    msg.callback = msg_handler;
    msg.callbackArg = fun_arg;

    pendingMessages[id] = msg;

    packetGenerator->inject_message(
        id, src, rank, convert_count_to_bytes(count, type), tag, rank);

    return 0;
}

void MyNetworkAPI::notify_message_complete(uint64_t messageId) {
    auto it = pendingMessages.find(messageId);

    if (it == pendingMessages.end()) {
        ASSERT_PRINT(false, "Message ID{messageId} not found in pending "
                            "messages of rank {rank}");  // TODO: fix syntax
        return;
    }

    auto callback = it->second.callback;
    auto arg = it->second.callbackArg;

    callback(arg);
    pendingMessages.erase(it);
}

void MyNetworkAPI::sim_schedule(timespec_t delta,
                                void (*fun_ptr)(void* fun_arg),
                                void* fun_arg) {

    ASSERT_PRINT(scheduler != nullptr, "MyNetworkAPI: scheduler is null.");

    sc_core::sc_time delay = timespecToSystemCTime(delta);

    scheduler->schedule(delay, fun_ptr, fun_arg);
}

// void MyNetworkAPI::
//     scheduleCallbackProcess() {  // TODO: maybe create multiple scheduler
//                                  // objects and keep a queue of them so many
//                                  // processes could run at the same time

//     while (true) {
//         wait(scheduledEvent);
//         wait(m_scheduledEvent.scheduledWaitTime);
//         auto callback = m_scheduledEvent.fun_ptr;
//         auto arg = m_scheduledEvent.fun_arg;
//         callback(arg);
//     }
// }

timespec_t MyNetworkAPI::sim_get_time() {
    return systemCTimeToTimespec(sc_core::sc_time_stamp());
}
