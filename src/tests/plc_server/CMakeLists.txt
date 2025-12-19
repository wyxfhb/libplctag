cmake_minimum_required(VERSION 3.10)

# Executable with all source files
add_executable(plc_server
    plc_server.c
    tag_storage.c
    udt_storage.c
    omron_storage.c
    protocol/eip_protocol.c
    protocol/cpf_protocol.c
    protocol/cip_message_router.c
    protocol/cip_path.c
    protocol/cip_object_registry.c
    protocol/objects/symbol_object.c
    protocol/objects/udt_object.c
    protocol/objects/identity_object.c
    protocol/objects/connection_manager.c
    protocol/objects/tag_name_server_omron.c
    protocol/objects/variable_object_omron.c
    protocol/objects/variable_type_object_omron.c
    ${TEST_UTILS_DIR}/args.c
    ${TEST_UTILS_DIR}/buf.c
    ${TEST_UTILS_DIR}/coro_net.c
    ${TEST_UTILS_DIR}/err.c
    ${TEST_UTILS_DIR}/log.c
    ${TEST_UTILS_DIR}/socket.c
    ${TEST_UTILS_DIR}/utils.c
)

# Include paths - access to utility headers
target_include_directories(plc_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_UTILS_DIR}
)


# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(plc_server PRIVATE ws2_32)
endif()

# Install to bin_dist
install(TARGETS plc_server DESTINATION bin_dist)
