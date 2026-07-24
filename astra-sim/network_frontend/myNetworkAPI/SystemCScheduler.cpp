#include "SystemCScheduler.h"

SystemCScheduler::SystemCScheduler(sc_core::sc_module_name name)
    : sc_core::sc_module(name) {

    SC_THREAD(run);
}

void SystemCScheduler::schedule(AstraSim::timespec_t delta,
                                void (*callback)(void*),
                                void* arg) {
    ScheduledCallback event;
    sc_core::sc_time delay = timespecToSystemCTime(delta);

    // std::cout << "Scheduling event with delay: " << delay.to_seconds() * 1e9
    //           << " nanoseconds, target time: "
    //           << (sc_core::sc_time_stamp() + delay).to_seconds() * 1e9
    //           << " nanoseconds" << std::endl;

    event.targetTime = sc_core::sc_time_stamp() + delay;
    event.eventId = m_nextEventId++;
    event.callback = callback;
    event.arg = arg;

    m_events.push(event);

    m_wakeup.notify(sc_core::SC_ZERO_TIME);
}

void SystemCScheduler::run() {
    uint64_t fired_count = 0;
    uint64_t wakeup_count = 0;

    wait(sc_core::SC_ZERO_TIME); // Ensure the simulation time starts at 0

    while (true) {
        if (m_events.empty()) {
            wakeup_count++;

            // std::cout << "[Scheduler " << name()
            //           << "] idle, waiting for wakeup. sc_time="
            //           << sc_core::sc_time_stamp()
            //           << " total_fired=" << fired_count
            //           << " total_wakeups=" << wakeup_count << std::endl;

            wait(m_wakeup);
            continue;
        }

        ScheduledCallback next = m_events.top();
        sc_core::sc_time now = sc_core::sc_time_stamp();

        if (next.targetTime <= now) {
            m_events.pop();
            fired_count++;

            // std::cout << "[Scheduler " << name() << "] fired callback #"
            //           << fired_count << " at sc_time=" << now
            //           << " queue_depth=" << m_events.size()
            //           << " target_time=" << next.targetTime
            //           << " eventId=" << next.eventId << std::endl;

            if (next.callback != nullptr) {
                next.callback(next.arg);
            } else {
                // std::cout << "[Scheduler " << name()
                //           << "] WARNING: null callback at sc_time=" << now
                //           << std::endl;
            }

            continue;
        }

        sc_core::sc_time delay = next.targetTime - now;

        // std::cout << "[Scheduler " << name() << "] sleeping for " << delay
        //           << " until target=" << next.targetTime << " sc_time=" << now
        //           << " queue_depth=" << m_events.size() << std::endl;

        wait(delay, m_wakeup);
    }
}
