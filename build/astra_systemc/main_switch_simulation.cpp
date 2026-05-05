#include <systemc>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Astra-sim
#include "astra-sim/system/Sys.hh"
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/common/AstraRemoteMemoryAPI.hh"

// MyNetworkAPI
#include "MyNetworkAPI.hh"

class NullRemoteMemoryAPI : public AstraSim::AstraRemoteMemoryAPI {
public:
    NullRemoteMemoryAPI() = default;
    ~NullRemoteMemoryAPI() override = default;
};

int sc_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // ------------------------------------------------------------
    // Fixed configuration
    // ------------------------------------------------------------

    constexpr int N = 4;

    const std::string workload_config =
        "inputs/workload/PLACEHOLDER_WORKLOAD";

    const std::string system_config =
        "inputs/system/my_system_config.json";

    const std::string comm_group_config =
        "inputs/comm_group/PLACEHOLDER_COMM_GROUP.json";

    const std::string run_name =
        "systemc_astrasim_test";

    const std::vector<int> physical_dims = {N};
    const std::vector<int> queues_per_dim = {1};

    constexpr double injection_scale = 1.0;
    constexpr double comm_scale = 1.0;
    constexpr bool rendezvous_enabled = false;

    const sc_core::sc_time step(1, sc_core::SC_NS);
    constexpr std::uint64_t max_steps = 100000000;

    std::cout << "[main] Starting Astra-sim + SystemC integration\n";
    std::cout << "[main] N ranks           = " << N << "\n";
    std::cout << "[main] workload config   = " << workload_config << "\n";
    std::cout << "[main] system config     = " << system_config << "\n";
    std::cout << "[main] comm group config = " << comm_group_config << "\n";

    // ------------------------------------------------------------
    // 1. Create your SystemC switch here.
    // ------------------------------------------------------------
    //
    // Example:
    // auto top_switch = std::make_unique<YourSystemCSwitch>("top_switch", N);
    //

    // ------------------------------------------------------------
    // 2. Create N network APIs.
    // ------------------------------------------------------------

    std::vector<std::unique_ptr<MyNetworkAPI>> network_apis;
    network_apis.reserve(N);

    for (int rank = 0; rank < N; ++rank) {
        network_apis.emplace_back(
            std::make_unique<MyNetworkAPI>(rank)
        );

        // Connect API rank -> switch port rank here.
        //
        // Example:
        // network_apis.back()->attach_switch(top_switch.get(), rank);
        // top_switch->bind_api(rank, network_apis.back().get());

        std::cout << "[main] Created MyNetworkAPI for rank "
                  << rank << "\n";
    }

    // ------------------------------------------------------------
    // 3. Create remote-memory backends.
    // ------------------------------------------------------------

    std::vector<std::unique_ptr<NullRemoteMemoryAPI>> remote_memories;
    remote_memories.reserve(N);

    for (int rank = 0; rank < N; ++rank) {
        remote_memories.emplace_back(
            std::make_unique<NullRemoteMemoryAPI>()
        );
    }

    // ------------------------------------------------------------
    // 4. Create N Astra-sim systems.
    // ------------------------------------------------------------

    std::vector<std::unique_ptr<AstraSim::Sys>> systems;
    systems.reserve(N);

    for (int rank = 0; rank < N; ++rank) {
        systems.emplace_back(
            std::make_unique<AstraSim::Sys>(
                rank,
                workload_config,
                comm_group_config,
                system_config,
                remote_memories[rank].get(),
                network_apis[rank].get(),
                physical_dims,
                queues_per_dim,
                injection_scale,
                comm_scale,
                rendezvous_enabled
            )
        );

        std::cout << "[main] Created AstraSim::Sys for rank "
                  << rank << "\n";
    }

    // ------------------------------------------------------------
    // 5. Initialize Astra-sim systems.
    // ------------------------------------------------------------

    for (int rank = 0; rank < N; ++rank) {
        const std::string sys_name =
            run_name + "_rank_" + std::to_string(rank);

        const bool ok = systems[rank]->initialize_sys(sys_name);

        std::cout << "[main] initialize_sys(rank="
                  << rank << ") returned " << ok << "\n";

        if (!ok) {
            std::cerr << "[main] ERROR: failed to initialize rank "
                      << rank << "\n";
            return 1;
        }
    }

    // ------------------------------------------------------------
    // 6. SystemC-owned simulation loop.
    // ------------------------------------------------------------

    std::uint64_t step_count = 0;

    while (true) {
        // Let Astra-sim process currently ready events.
        for (int rank = 0; rank < N; ++rank) {
            systems[rank]->call_events();
        }

        // Advance SystemC.
        sc_core::sc_start(step);
        ++step_count;

        if (step_count % 1000 == 0) {
            std::cout << "[main] step=" << step_count
                      << " sc_time=" << sc_core::sc_time_stamp()
                      << "\n";
        }

        // --------------------------------------------------------
        // Completion check.
        // Replace network_done with your actual switch pending check.
        // --------------------------------------------------------

        bool astra_done = true;

        for (int rank = 0; rank < N; ++rank) {
            if (systems[rank]->pending_events != 0) {
                astra_done = false;
                break;
            }
        }

        bool network_done = true;

        // Replace this:
        //
        // network_done = top_switch->num_in_flight_messages() == 0;
        // network_done = top_switch->empty();
        // network_done = !top_switch->has_pending_messages();

        if (astra_done && network_done) {
            std::cout << "[main] Done: Astra-sim workload complete "
                      << "and SystemC network empty\n";
            break;
        }

        if (step_count >= max_steps) {
            std::cerr << "[main] ERROR: reached max_steps without completion\n";
            std::cerr << "[main] Likely causes:\n";
            std::cerr << "       1. Network callbacks are not called.\n";
            std::cerr << "       2. Astra-sim still has pending events.\n";
            std::cerr << "       3. SystemC switch still has in-flight messages.\n";
            break;
        }
    }

    sc_core::sc_stop();

    std::cout << "[main] Final SystemC time = "
              << sc_core::sc_time_stamp() << "\n";

    return 0;
}