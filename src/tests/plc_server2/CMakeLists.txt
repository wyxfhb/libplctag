
# Define source files for plc_server2
set(PLC_SERVER2_SOURCES
    # Server
    server/main.c

    # Protocol layers
    protocols/eip/eip_protocol.c
    protocols/cpf/cpf_protocol.c
    protocols/cip/cip_protocol.c
    protocols/cip/cip_registry.c
    protocols/cip/cip_path.c

    # Generic PLC utilities
    plcs/generic/tag_storage.c
    plcs/generic/udt_storage.c

    # AB ControlLogix
    plcs/ab/ab_context.c
    plcs/ab/ab_plc_logix.c
    plcs/ab/symbol_object.c
    plcs/ab/connection_manager.c

    # Shared test utilities (include directly)
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/args.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/data_reader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/packet_builder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/log_modules.def
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/socket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/coro_net.c
)

# Create the plc_server2 executable
add_executable(plc_server2 ${PLC_SERVER2_SOURCES})

# Include directories
target_include_directories(plc_server2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/tests/utils
)

# Utilities are compiled directly into plc_server2

# C standard and compiler flags
set_target_properties(plc_server2 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Optional: Add compiler warnings
if(UNIX)
    target_compile_options(plc_server2 PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
endif()

