message("Building test utilities.")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG}")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_MINSIZEREL}")
endif()

# message("CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")


# Define the source files for ab_server
set(AB_SERVER_FILES
    # Ethernet/IP Protocol Implementation
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_omron.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_omron.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_logix.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cip_logix.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cpf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/cpf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/eip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/ethernet_ip/eip.h

    # PLC-Specific Implementations
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/plcs/omron_plc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/plcs/omron_plc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/plcs/controllogix_plc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/plcs/controllogix_plc.h

    # Core Infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/compat.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/fairness.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/fairness.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/memory.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/memory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/pccc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/pccc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/plc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/slice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ab_server/src/utils.h

    # Shared utilities from src/tests/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/buf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/buf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/coro_net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/coro_net.h
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/err.h
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/log_modules.def
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/socket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/socket.h
)

# Add ab_server executable
add_executable(ab_server ${AB_SERVER_FILES})
target_link_libraries(ab_server PUBLIC ${EXTRA_LINKER_LIBS})

# Link math library for ab_server (fairness.c uses math.h)
# Math functions are in libm on Unix but in the C runtime on Windows
if(UNIX)
    target_link_libraries(ab_server PUBLIC m)
endif()

# if(STATIC_LINK_OPTIONS)
#     set(EXTRA_LINK_OPTIONS "${EXTRA_LINK_OPTIONS} ${STATIC_LINK_OPTIONS}")
# endif()

# if(EXTRA_LINK_OPTIONS)
    # target_link_options(ab_server PUBLIC ${EXTRA_LINK_OPTIONS})
    target_link_options(ab_server PRIVATE ${STATIC_C_LINKER_OPTIONS} ${EXTRA_LINK_FLAGS})
# endif()

# Build the Modbus TCP server
# This is a lightweight implementation using the coro_net event-based reactor
message("Building Modbus TCP server.")
add_subdirectory("modbus_server")


