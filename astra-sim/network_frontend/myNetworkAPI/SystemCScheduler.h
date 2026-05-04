#pragma once

#include "TimeConversion.hh"
#include <cstdint>
#include <queue>
#include <systemc>
#include <vector>

struct ScheduledCallback {
    sc_core::sc_time targetTime;
    std::uint64_t eventId;

    void (*callback)(void*);
    void* arg;
};

struct ScheduledCallbackCompare {
    bool operator()(const ScheduledCallback& a,
                    const ScheduledCallback& b) const {
        if (a.targetTime == b.targetTime) {
            return a.eventId > b.eventId;
        }

        return a.targetTime > b.targetTime;
    }
};

class SystemCScheduler : public sc_core::sc_module {
  public:
    SC_HAS_PROCESS(SystemCScheduler);

    explicit SystemCScheduler(sc_core::sc_module_name name)
        : sc_core::sc_module(name) {
        SC_THREAD(run);
    }

    void schedule(timespec_t delta, void (*callback)(void*), void* arg) {
        ScheduledCallback event;
        sc_core::sc_time delay = timespecToSystemCTime(delta);

        event.targetTime = sc_core::sc_time_stamp() + delay;
        event.eventId = m_nextEventId++;
        event.callback = callback;
        event.arg = arg;

        m_events.push(event);

        m_wakeup.notify(sc_core::SC_ZERO_TIME);
    }

  private:
    std::priority_queue<ScheduledCallback,
                        std::vector<ScheduledCallback>,
                        ScheduledCallbackCompare>
        m_events;

    sc_core::sc_event m_wakeup;
    std::uint64_t m_nextEventId = 0;

    void run() {
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
};
