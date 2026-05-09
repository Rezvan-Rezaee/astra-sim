#include <systemc>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
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

enum class SwitchType { paradox = 0, hierXbar };
#define USE_DSS

static size_t parseUnsignedArg(const char* arg, const std::string& name) {
    try {
        std::string s(arg);

        if (s.empty() || s[0] == '-') {
            throw std::invalid_argument("negative or empty value");
        }

        size_t pos = 0;
        unsigned long long value = std::stoull(s, &pos, 10);

        if (pos != s.size()) {
            throw std::invalid_argument("contains non-numeric characters");
        }

        if (value > std::numeric_limits<size_t>::max()) {
            throw std::out_of_range("too large for size_t");
        }

        return static_cast<size_t>(value);
    } catch (const std::exception& e) {
        std::cerr << "Invalid " << name << ": " << arg << " (" << e.what()
                  << ")" << std::endl;
        std::exit(1);
    }
}

static double parseDoubleArg(const char* arg, const std::string& name) {
    try {
        std::string s(arg);

        if (s.empty() || s[0] == '-') {
            throw std::invalid_argument("negative or empty value");
        }

        size_t pos = 0;
        double value = std::stod(s, &pos);

        if (pos != s.size()) {
            throw std::invalid_argument("contains non-numeric characters");
        }

        if (value <= 0.0) {
            throw std::invalid_argument("must be > 0");
        }

        return value;
    } catch (const std::exception& e) {
        std::cerr << "Invalid " << name << ": " << arg
                  << " (" << e.what() << ")" << std::endl;
        std::exit(1);
    }
}
class NullRemoteMemoryAPI : public AstraSim::AstraRemoteMemoryAPI {
  public:
    NullRemoteMemoryAPI() = default;
    ~NullRemoteMemoryAPI() override = default;
};

int sc_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (argc != 5) {
        ASSERT_PRINT(
            false,
            "Usage: %s <num_ports> <bytes_per_cell> <collective_communication_type> <communication_scale>\n"
            "  num_ports: Number of switch ports (e.g., 4, 8, 16)\n"
            "  bytes_per_cell: Number of bytes per cell (e.g., 64, 128)\n"
            "  collective_communication_type: Type of collective communication (0 for all_reduce, 1 for all_gather, 2 for all_to_all)\n"
            "  communication_scale: Scale factor for communication (e.g., 1.0, 2.0)\n",
            // "  switch_type: Type of switch to simulate (0 for paradox, "
            // "1 for hierXbar)\n",
            argv[0]);
    }

    // const int switchTypeInt = parseUnsignedArg(argv[1], "switch_type");
    const size_t numPorts = parseUnsignedArg(argv[1], "num_ports");
    const size_t bytesPerCell = parseUnsignedArg(argv[2], "bytes_per_cell");
    const size_t communicationType = parseUnsignedArg(argv[3], "collective_communication_type");
    const double comm_scale = parseDoubleArg(argv[4], "communication_scale");  // times workload size by this factor (1 MiB workload default)
    const size_t et_comm_scale = parseUnsignedArg(argv[4], "et scale");
    
    std::string communicationTypeStr;
    switch (communicationType) {
        case 0:
            communicationTypeStr = "all_reduce";
            break;
        case 1:
            communicationTypeStr = "all_gather";
            break;
        case 2:
            communicationTypeStr = "all_to_all";
            break;
        default:
            std::cerr << "Invalid collective communication type: " << communicationType
                      << ". Valid values are 0 (all_reduce), 1 (all_gather), and 2 (all_to_all)." << std::endl;
            return 1;
    }

    std::cout << "Starting SystemC AstraSim Simulation\n";

    std::string workloadDir = "/mnt/c/Users/rezva/Documents/paradox/astra-sim/"
                              "examples/workload/microbenchmarks/" + communicationTypeStr + "/" +
                              std::to_string(numPorts) + "npus_" + std::to_string(et_comm_scale) + "MB/" + communicationTypeStr;
    const std::string workload_config = workloadDir;

    const std::string system_config =
        "/mnt/c/Users/rezva/Documents/paradox/astra-sim/inputs/system/"
        "my_system_config.json";

    const std::string comm_group_config =
        "/mnt/c/Users/rezva/Documents/paradox/astra-sim/build/astra_systemc/"
        "network_cfg_.json";

    const std::string remote_memory_configuration =
        "/mnt/c/Users/rezva/Documents/paradox/astra-sim/examples/remote_memory/"
        "analytical/no_memory_expansion.json";
    //    "/../../../build/astra_systemc/no_memory_expansion.json";

    const std::string run_name = "systemc_astrasim_test";

    const std::vector<int> physical_dims = {static_cast<int>(numPorts)};
    const std::vector<int> queues_per_dim = {1};

    constexpr double injection_scale = 1.0;
    constexpr bool rendezvous_enabled = false;

#if defined(USE_DSS)
    std::cout << "Using DSS switch model\n";
#else
    std::cout << "Using Hierarchical Crossbar switch model\n";
#endif

    std::cout << "numPorts: " << numPorts << ", bytesPerCell: " << bytesPerCell
              << "\n";

    // ------------------------------------------------------------
    Packet::resetPacketID();

    sc_report_handler::set_verbosity_level(SC_HIGH);
    sc_report_handler::set_actions(SC_ERROR, SC_DISPLAY | SC_LOG);

    const auto t0 = std::chrono::steady_clock::now();

    ScPacketGen packetGen(numPorts, 50);

#if defined(USE_DSS)
    SwitchDSS swParadox(numPorts, "DSS", 1200000);
    ScSwitchDSS dut("paradox", swParadox, packetGen, 3);
    // dut.enableDebug(swParams.debugEnabled);
#else
    SwitchHierXbar swHierXbar(numPorts, 1200000, "HierXbar");
    ScSwitchHierXbar dut("hierXbar", swHierXbar, packetGen, 3);
#endif

    dut.setPayloadEnabledAll(true);

    dut.setStatisticsParameters(20, 40);
    dut.setCycleStatsEnabled(true);
    dut.setActiveCycles(2000000);

    PacketGeneratorInterface packetGeneratorInterface(numPorts, bytesPerCell, 10);
    dut.setPacketGeneratorInterface(&packetGeneratorInterface);

    // ------------------------------------------------------------
    std::vector<std::unique_ptr<SystemCScheduler>> schedulers;
    schedulers.reserve(static_cast<int>(numPorts));

    for (int i = 0; i < static_cast<int>(numPorts); ++i) {
        schedulers.emplace_back(std::make_unique<SystemCScheduler>(
            sc_core::sc_gen_unique_name("SystemCScheduler")));
    }

    std::vector<std::unique_ptr<AstraSim::MyNetworkAPI>> network_apis;
    network_apis.reserve(static_cast<int>(numPorts));
    for (int rank = 0; rank < static_cast<int>(numPorts); ++rank) {
        network_apis.emplace_back(std::make_unique<AstraSim::MyNetworkAPI>(
            rank, &packetGeneratorInterface, schedulers.at(rank).get()));
    }

    const auto memory_api =
        std::make_unique<Analytical::AnalyticalRemoteMemory>(
            remote_memory_configuration);

    std::vector<std::unique_ptr<AstraSim::Sys>> systems;
    systems.reserve(static_cast<int>(numPorts));
    for (int rank = 0; rank < static_cast<int>(numPorts); ++rank) {
        systems.emplace_back(std::make_unique<AstraSim::Sys>(
            rank, workload_config, comm_group_config, system_config,
            memory_api.get(), network_apis.at(rank).get(), physical_dims,
            queues_per_dim, injection_scale, comm_scale, rendezvous_enabled));
    }

    // Initiate ASTRA-sim simulation
    for (int i = 0; i < static_cast<int>(numPorts); i++) {
        systems[i]->workload->fire();
    }

    sc_core::sc_start();

    for (int rank = 0; rank < static_cast<int>(numPorts); ++rank) {
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
