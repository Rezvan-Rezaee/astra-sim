#include "MyNetworkAPI.hh"
#include "astra-sim/common/Logging.hh"

using namespace AstraSim;

AstraSimAnalytical::ChunkIdGenerator MyNetworkAPI::chunk_id_generator = {};
AstraSimAnalytical::CallbackTracker MyNetworkAPI::callback_tracker = {};

static std::shared_ptr<spdlog::logger>& net_log() {
    static auto l = LoggerFactory::get_logger("network_api");
    return l;
}

MyNetworkAPI::MyNetworkAPI(int rank,
                           PacketGeneratorInterface* pg,
                           SystemCScheduler* sched)
    : AstraNetworkAPI(rank),
      packetGenerator(pg),
      scheduler(sched) {
    ASSERT_PRINT(packetGenerator != nullptr,
                 "MyNetworkAPI: packetGenerator is null.");
    ASSERT_PRINT(scheduler != nullptr, "MyNetworkAPI: scheduler is null.");
}

int MyNetworkAPI::sim_send(void* buffer,
                           uint64_t count,
                           int type,
                           int dst,
                           int tag,
                           sim_request* request,
                           void (*msg_handler)(void* fun_arg),
                           void* fun_arg) {

    ASSERT_PRINT(packetGenerator != nullptr,
                 "MyNetworkAPI: packetGenerator is null.");
    ASSERT_PRINT(msg_handler != nullptr,
                 "MyNetworkAPI::sim_send: msg_handler is null.");
    ASSERT_PRINT(count > 0, "MyNetworkAPI::sim_send: count must be > 0.");

    const int src = rank;

    const int chunk_id =
        chunk_id_generator.create_send_chunk_id(tag, src, dst, count);

    net_log()->info("sim_send: rank={} -> dst={} tag={} count={} chunk_id={}",
                    src, dst, tag, count, chunk_id);

    auto entry = callback_tracker.search_entry(tag, src, dst, count, chunk_id);

    if (entry.has_value()) {
        entry.value()->register_send_callback(msg_handler, fun_arg);
    } else {
        auto* new_entry =
            callback_tracker.create_new_entry(tag, src, dst, count, chunk_id);

        new_entry->register_send_callback(msg_handler, fun_arg);
    }

    auto* completion_info =
        new MessageCompletionInfo{tag, src, dst, count, chunk_id};

    const size_t physical_message_id = packetGenerator->generate_message_id();

    packetGenerator->inject_message(
        physical_message_id, src, dst, tag, count, type,
        &MyNetworkAPI::process_message_arrival, completion_info);

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

    ASSERT_PRINT(msg_handler != nullptr,
                 "MyNetworkAPI::sim_recv: msg_handler is null.");
    ASSERT_PRINT(count > 0, "MyNetworkAPI::sim_recv: count must be > 0.");

    const int dst = rank;

    const int chunk_id =
        chunk_id_generator.create_recv_chunk_id(tag, src, dst, count);

    net_log()->info("sim_recv: rank={} <- src={} tag={} count={} chunk_id={}",
                    dst, src, tag, count, chunk_id);

    auto entry = callback_tracker.search_entry(tag, src, dst, count, chunk_id);

    if (entry.has_value()) {
        if (entry.value()->is_transmission_finished()) {
            callback_tracker.pop_entry(tag, src, dst, count, chunk_id);

            const auto delta = AstraSim::timespec_t{AstraSim::NS, 0};
            sim_schedule(delta, msg_handler, fun_arg);
        } else {
            entry.value()->register_recv_callback(msg_handler, fun_arg);
        }
    } else {
        auto* new_entry =
            callback_tracker.create_new_entry(tag, src, dst, count, chunk_id);

        new_entry->register_recv_callback(msg_handler, fun_arg);
    }

    return 0;
}

void MyNetworkAPI::process_message_arrival(void* args) {
    ASSERT_PRINT(args != nullptr,
                 "MyNetworkAPI::process_message_arrival: args is null.");

    auto* info = static_cast<MessageCompletionInfo*>(args);

    const int tag = info->tag;
    const int src = info->src;
    const int dst = info->dst;
    const uint64_t count = info->count;
    const int chunk_id = info->chunk_id;

    delete info;

    auto entry = callback_tracker.search_entry(tag, src, dst, count, chunk_id);

    ASSERT_PRINT(
        entry.has_value(),
        "MyNetworkAPI::process_message_arrival: no callback tracker entry.");

    if (entry.value()->both_callbacks_registered()) {
        net_log()->info(
            "arrival: src={} dst={} tag={} count={} chunk_id={} -> both "
            "callbacks registered, invoking send+recv",
            src, dst, tag, count, chunk_id);
        entry.value()->invoke_send_handler();
        entry.value()->invoke_recv_handler();

        callback_tracker.pop_entry(tag, src, dst, count, chunk_id);
    } else {
        net_log()->info(
            "arrival: src={} dst={} tag={} count={} chunk_id={} -> recv not "
            "yet registered, marking transmission done",
            src, dst, tag, count, chunk_id);
        entry.value()->invoke_send_handler();
        entry.value()->set_transmission_finished();
    }
}

void MyNetworkAPI::sim_schedule(AstraSim::timespec_t delta,
                                void (*fun_ptr)(void* fun_arg),
                                void* fun_arg) {

    ASSERT_PRINT(scheduler != nullptr, "MyNetworkAPI: scheduler is null.");
    ASSERT_PRINT(fun_ptr != nullptr, "MyNetworkAPI::sim_schedule: fun_ptr is null.");

    net_log()->debug("sim_schedule: delta={} units={}",
                     delta.time_val, static_cast<int>(delta.time_res));
    scheduler->schedule(delta, fun_ptr, fun_arg);
}

AstraSim::timespec_t MyNetworkAPI::sim_get_time() {
    ASSERT_PRINT(packetGenerator != nullptr, "MyNetworkAPI: packetGenerator is null.");
    return systemCTimeToTimespec(packetGenerator->get_current_time());
}

void MyNetworkAPI::sim_notify_finished() {
    ASSERT_PRINT(packetGenerator != nullptr, "MyNetworkAPI: packetGenerator is null.");

    net_log()->info("sim_notify_finished: rank={}", rank);
    simulation_finished = true;
    packetGenerator->notify_finished(rank);
}
