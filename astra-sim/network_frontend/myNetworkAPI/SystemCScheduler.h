#pragma once

#include "TimeConversion.h"
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
    SystemCScheduler(sc_core::sc_module_name name);

    void schedule(AstraSim::timespec_t delta, void (*callback)(void*), void* arg);

  private:
    std::priority_queue<ScheduledCallback,
                        std::vector<ScheduledCallback>,
                        ScheduledCallbackCompare>
        m_events;

    sc_core::sc_event m_wakeup;
    std::uint64_t m_nextEventId = 0;

    void run();
};
