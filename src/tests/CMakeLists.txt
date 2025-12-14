message("Building test utilities.")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG}")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_MINSIZEREL}")
endif()


set(TEST_UTILS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/utils)

# Build the AB server (EtherNet/IP simulator)
add_subdirectory("ab_server")

# Build the Modbus TCP server
# This is a lightweight implementation using the coro_net event-based reactor
message("Building Modbus TCP server.")
add_subdirectory("modbus_server")

# Build the PLC Server (EtherNet/IP simulator)
# Greenfield implementation using event-driven coroutine architecture
message("Building EtherNet/IP PLC server.")
add_subdirectory("plc_server")


