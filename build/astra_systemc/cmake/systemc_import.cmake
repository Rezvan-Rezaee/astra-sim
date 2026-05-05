if(TARGET systemc_import)
    return()
endif()

if(NOT DEFINED ENV{SYSTEMC_HOME})
    message(FATAL_ERROR "SYSTEMC_HOME environment variable is not set.")
endif()

set(SYSTEMC_HOME $ENV{SYSTEMC_HOME})

add_library(systemc_import INTERFACE)

target_include_directories(systemc_import
    INTERFACE
        "${SYSTEMC_HOME}/include"
)

target_link_directories(systemc_import
    INTERFACE
        "${SYSTEMC_HOME}/lib"
)

target_link_libraries(systemc_import
    INTERFACE
        systemc
)