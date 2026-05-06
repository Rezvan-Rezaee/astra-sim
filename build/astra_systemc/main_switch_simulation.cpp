#include <systemc>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Astra-sim
#include "astra-sim/common/AstraNetworkAPI.hh"
#include "astra-sim/common/AstraRemoteMemoryAPI.hh"
#include "astra-sim/system/Sys.hh"
#include "extern/remote_memory_backend/analytical/AnalyticalRemoteMemory.hh"

// MyNetworkAPI
#include "MyNetworkAPI.hh"

// SystemC switch and packet generator
#include "Packet.h"
#include "PacketGeneratorInterface.h"
#include "ScPacketGen.h"
#include "ScSwitchBox.h"
#include "ScSwitchDSS.h"
#include "ScSwitchHierXbar.h"
#include "Switch.h"
#include "SwitchBanyan.h"
#include "SwitchCrossbar.h"
#include "SwitchDSS.h"
#include "SwitchHierXbar.h"
// #include "runtimeMonitor.h"
#include "support/ArgParser.h"
#include "support/Args.h"
#include "support/Cmds.h"

#include "SystemCScheduler.h"

const size_t NumPorts = 16;

enum class SwitchType { paradox = 0, hierXbar };

#define USE_DSS

class NullRemoteMemoryAPI : public AstraSim::AstraRemoteMemoryAPI {
  public:
    NullRemoteMemoryAPI() = default;
    ~NullRemoteMemoryAPI() override = default;
};

int sc_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    constexpr int N = 16;  // Number of ranks/NPUs

    const std::string workload_config =
        "inputs/workload/microbenchmarks/all_gather/16npus_1MB/all_gather";

    const std::string system_config = "inputs/system/my_system_config.json";

    const std::string comm_group_config =
        "build/astra_systemc/network_cfg_.json";

    const std::string remote_memory_configuration =
        "examples/remote_memory/analytical/no_memory_expansion.json";

    const std::string run_name = "systemc_astrasim_test";

    const std::vector<int> physical_dims = {N};
    const std::vector<int> queues_per_dim = {1};

    constexpr double injection_scale = 1.0;
    constexpr double comm_scale = 1.0;
    constexpr bool rendezvous_enabled = false;

    const sc_core::sc_time step(
        1, sc_core::SC_NS);  // TODO: adjust step time as needed to match clock
                             // period

    // ------------------------------------------------------------
    Packet::resetPacketID();

    sc_report_handler::set_verbosity_level(SC_HIGH);
    sc_report_handler::set_actions(SC_ERROR, SC_DISPLAY | SC_LOG);

    const auto t0 = std::chrono::steady_clock::now();

    ScPacketGen packetGen(NumPorts, 50);

#if defined(USE_DSS)
    SwitchDSS swParadox(NumPorts, "DSS", 1200000);
    ScSwitchDSS dut("paradox", swParadox, packetGen, 3);
    // dut.enableDebug(swParams.debugEnabled);
#else
    SwitchHierXbar swHierXbar(NumPorts, 1200000, "HierXbar");
    ScSwitchHierXbar dut("hierXbar", swHierXbar, packetGen, 3);
#endif

    dut.setPayloadEnabledAll(true);

    dut.setStatisticsParameters(20, 40);
    dut.setCycleStatsEnabled(false);

    PacketGeneratorInterface packetGeneratorInterface(NumPorts, 8, 10);
    dut.setPacketGeneratorInterface(&packetGeneratorInterface);

    // ------------------------------------------------------------

    std::vector<SystemCScheduler> scheduler(N);

    std::vector<std::unique_ptr<AstraSim::MyNetworkAPI>> network_apis;
    network_apis.reserve(N);
    for (int rank = 0; rank < N; ++rank) {
        network_apis.emplace_back(std::make_unique<AstraSim::MyNetworkAPI>(
            rank, &packetGeneratorInterface, &scheduler.at(rank)));
    }

    const auto memory_api =
        std::make_unique<Analytical::AnalyticalRemoteMemory>(remote_memory_configuration);

    std::vector<std::unique_ptr<AstraSim::Sys>> systems;
    systems.reserve(N);
    for (int rank = 0; rank < N; ++rank) {
        systems.emplace_back(std::make_unique<AstraSim::Sys>(
            rank, workload_config, comm_group_config, system_config,
            memory_api.get(), network_apis.at(rank).get(), physical_dims,
            queues_per_dim, injection_scale, comm_scale, rendezvous_enabled));
    }

    for (int rank = 0; rank < N; ++rank) {
        const std::string sys_name = run_name + "_rank_" + std::to_string(rank);

        const bool ok = systems.at(rank)->initialize_sys(sys_name);

        std::cout << "[main] initialize_sys(rank=" << rank << ") returned "
                  << ok << "\n";

        if (!ok) {
            std::cerr << "[main] ERROR: failed to initialize rank " << rank
                      << "\n";
            return 1;
        }
    }

    sc_core::sc_start();

    // for (int rank = 0; rank < N; ++rank) {
    //     systems[rank]->call_events();
    // }

    for (int rank = 0; rank < N; ++rank) {
        if (systems[rank]->pending_events != 0) {
            ASSERT_PRINT(false,
                         "Rank %d still has pending events at the end of "
                         "simulation: %zu\n",
                         rank, systems[rank]->pending_events);
        }
    }

#if defined(USE_DSS)
    dut.printSimulationResults_dss();
#else
    dut.printSimulationResults_HierXbar();
#endif
    dut.printStats();

    const auto t1 = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = t1 - t0;

    std::cout << ", " << elapsed.count() << "\n";

    return 0;
}
