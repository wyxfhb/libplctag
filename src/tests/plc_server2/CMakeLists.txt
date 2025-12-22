# PLC Server 2 - Refactored Architecture
# CMakeLists.txt
#
# Build plc_server2 with modular protocol layers and PLC-specific implementations

cmake_minimum_required(VERSION 3.10)

# Define source files for plc_server2
set(PLC_SERVER2_SOURCES
    # Server
    server/main.c

    # Context registry
    context/context_registry.c

    # Protocol layers
    protocols/eip/eip_protocol.c
    protocols/cpf/cpf_protocol.c
    protocols/cip/cip_protocol.c
    protocols/cip/cip_registry.c
    # protocols/cip/cip_path.c  # TODO: Port from plc_server

    # Generic PLC utilities
    plcs/generic/tag_storage.c
    plcs/generic/udt_storage.c

    # AB ControlLogix
    plcs/ab/ab_context.c
    plcs/ab/ab_logix.c
    plcs/ab/symbol_object.c

    # Shared test utilities (include directly)
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/buf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/log_modules.def
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/utils.c
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

# Set output directory
set_target_properties(plc_server2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Custom target for testing
add_custom_target(run_plc_server2
    COMMAND plc_server2 --help
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running plc_server2 help"
)

# Custom target for building and running
add_custom_target(test_plc_server2
    COMMAND plc_server2 --plc-type controllogix
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Testing plc_server2"
    DEPENDS plc_server2
)
