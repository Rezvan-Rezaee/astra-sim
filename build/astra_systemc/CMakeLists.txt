cmake_minimum_required(VERSION 3.15)

project(AstraSim_SystemC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

################## add if runnung with build_flags.sh
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-g -O0 -fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
######################################################

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/systemc_import.cmake)

set(TOP_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(SYSTEMC_SWITCH_ROOT "${TOP_FOLDER}/ParaDOX/cpp/systemC")

set(CPP_ROOT "${SYSTEMC_SWITCH_ROOT}/..")
set(CORE_INC "${CPP_ROOT}/inc")
set(CORE_SRC "${CPP_ROOT}/src")
set(SC_INC   "${SYSTEMC_SWITCH_ROOT}/inc")
set(SC_SRC   "${SYSTEMC_SWITCH_ROOT}/src")

add_library(paradox_systemc_switch STATIC
    "${SC_SRC}/ScSwitchHierXbar.cpp"
    "${SC_SRC}/ScSwitchDSS.cpp"
    "${SC_SRC}/ScSwitchBox.cpp"
    "${SC_SRC}/ScSwitch.cpp"
    "${SC_SRC}/ScFifo.cpp"
    "${SC_SRC}/PacketGeneratorInterface.cpp"

    "${CORE_SRC}/basics.cpp"
    "${CORE_SRC}/Packet.cpp"
    "${CORE_SRC}/SingleFifo.cpp"
    "${CORE_SRC}/Fifo.cpp"
    "${CORE_SRC}/PacketGenerator.cpp"
    "${CORE_SRC}/Ports.cpp"
    "${CORE_SRC}/Draw.cpp"
    "${CORE_SRC}/Switch.cpp"
    "${CORE_SRC}/PacketIO.cpp"
    "${CORE_SRC}/SwitchBox.cpp"
    "${CORE_SRC}/SwitchIdeal.cpp"
    "${CORE_SRC}/SwitchCrossbar.cpp"
    "${CORE_SRC}/SwitchBanyan.cpp"
    "${CORE_SRC}/support/Cmds.cpp"
    "${CORE_SRC}/SwitchDSS.cpp"
    "${CORE_SRC}/SwitchHierXbar.cpp"
)


target_include_directories(paradox_systemc_switch
    PUBLIC
        "${SC_INC}"
        "${CORE_INC}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../astra-sim/network_frontend/myNetworkAPI"
)

target_link_libraries(paradox_systemc_switch
    PUBLIC
        systemc_import
)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    AstraSim
)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../astra-sim/network_frontend/myNetworkAPI
    AstraSim_MyNetworkAPI
)


add_executable(AstraSim_SystemC
    main_switch_simulation.cpp
)

target_link_libraries(AstraSim_SystemC
    PRIVATE
        AstraSim
        AstraSim_MyNetworkAPI
        paradox_systemc_switch
        systemc_import
)

set_target_properties(AstraSim_SystemC
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
        BUILD_RPATH "${SYSTEMC_HOME}/lib"
)