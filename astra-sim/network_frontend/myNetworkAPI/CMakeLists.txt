cmake_minimum_required(VERSION 3.15)

project(AstraSim_MyNetworkAPI LANGUAGES CXX)

add_library(AstraSim_MyNetworkAPI STATIC
    MyNetworkAPI.cc
    SystemCScheduler.cpp
)

target_include_directories(AstraSim_MyNetworkAPI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(AstraSim_MyNetworkAPI
    PUBLIC
        AstraSim
        paradox_systemc_switch
        systemc_import
)