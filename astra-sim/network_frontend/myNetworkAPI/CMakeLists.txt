cmake_minimum_required(VERSION 3.15)

project(AstraSim_MyNetworkAPI LANGUAGES CXX)

set(ANALYTICAL_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../analytical/common")
set(ANALYTICAL_BACKEND_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/network_backend/analytical/common")

add_library(AstraSim_MyNetworkAPI STATIC
    MyNetworkAPI.cc
    SystemCScheduler.cpp
    ${ANALYTICAL_COMMON}/CallbackTracker.cc
    ${ANALYTICAL_COMMON}/CallbackTrackerEntry.cc
    ${ANALYTICAL_COMMON}/ChunkIdGenerator.cc
    ${ANALYTICAL_COMMON}/ChunkIdGeneratorEntry.cc
    ${ANALYTICAL_BACKEND_COMMON}/event-queue/Event.cpp
    ${ANALYTICAL_BACKEND_COMMON}/event-queue/EventList.cpp
    ${ANALYTICAL_BACKEND_COMMON}/event-queue/EventQueue.cpp
    ${ANALYTICAL_BACKEND_COMMON}/NetworkFunction.cpp
    ${ANALYTICAL_BACKEND_COMMON}/network-parser/NetworkParser.cpp
)

target_include_directories(AstraSim_MyNetworkAPI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../analytical/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/network_backend/analytical/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/network_backend/analytical/include/astra-network-analytical
)

target_link_libraries(AstraSim_MyNetworkAPI
    PUBLIC
        AstraSim
        paradox_systemc_switch
        systemc_import
)