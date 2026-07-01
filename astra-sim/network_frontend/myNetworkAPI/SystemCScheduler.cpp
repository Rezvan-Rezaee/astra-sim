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

    event.targetTime = sc_core::sc_time_stamp() + delay;
    event.eventId = m_nextEventId++;
    event.callback = callback;
    event.arg = arg;

    m_events.push(event);

    m_wakeup.notify(sc_core::SC_ZERO_TIME);
}

void SystemCScheduler::run() {
    while (true) {
        if (m_events.empty()) {
            wait(m_wakeup);
            continue;
        }

        ScheduledCallback next = m_events.top();
        sc_core::sc_time now = sc_core::sc_time_stamp();

        if (next.targetTime <= now) {
            m_events.pop();

            if (next.callback != nullptr) {
                next.callback(next.arg);
            }

            continue;
        }

        wait(next.targetTime - now, m_wakeup);
    }
}
