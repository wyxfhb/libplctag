cmake_minimum_required(VERSION 3.10)

# Executable with all source files
add_executable(plc_server
    plc_server.c
    tag_storage.c
    omron_storage.c
    protocol/eip_protocol.c
    protocol/cpf_protocol.c
    protocol/cip_message_router.c
    protocol/cip_path.c
    protocol/cip_object_registry.c
    protocol/objects/symbol_object_micro800.c
    protocol/objects/symbol_object_controllogix.c
    protocol/objects/identity_object.c
    protocol/objects/tag_name_server_omron.c
    protocol/objects/variable_object_omron.c
    protocol/objects/variable_type_object_omron.c
)

# Include paths - access to utility headers
target_include_directories(plc_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils
)

# Link utility libraries
target_link_libraries(plc_server PRIVATE
    utils           # buf.h, socket.h, log.h, err.h, args.h, utils.h
    coro_net        # coro_net.h (event loop + coroutines)
)

# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(plc_server PRIVATE ws2_32)
endif()

# Install to bin_dist
install(TARGETS plc_server DESTINATION bin_dist)
