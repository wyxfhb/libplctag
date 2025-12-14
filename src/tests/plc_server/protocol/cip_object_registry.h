#pragma once

#include <stdint.h>
#include "../../utils/buf.h"
#include "../../utils/err.h"

/* Forward declarations */
typedef struct plc_context_s plc_context_t;
typedef struct cip_path_s cip_path_t;
typedef struct cip_object_class_s cip_object_class_t;
typedef struct cip_object_instance_s cip_object_instance_t;

/* ============================================================================
 * Service Handler Signature
 * ============================================================================ */

/**
 * CIP Service Handler Function Signature
 *
 * All CIP service handlers follow this signature.
 * Handlers read request parameters from the input buffer,
 * process the request, and write response data to output buffer.
 */
typedef util_err_t (*cip_service_handler_t)(uint8_t service,                 /* Service code (0x4C, 0x4D, etc.) */
                                            const cip_path_t *path,          /* Parsed path (may have symbolic segment) */
                                            buf_t *request,                  /* Request data (positioned after service+path) */
                                            buf_t *response,                 /* Response buffer to write data to */
                                            cip_object_instance_t *instance, /* Target instance */
                                            plc_context_t *plc               /* PLC context (for tags, etc.) */
);

/* ============================================================================
 * CIP Object Instance
 * ============================================================================ */

/**
 * CIP Object Instance
 *
 * Represents a specific instance of a CIP object class.
 */
typedef struct cip_object_instance_s {
    cip_object_class_t *object_class; /* Pointer to class definition */
    uint32_t instance_id;             /* Instance ID */
    void *instance_data;              /* Class-specific instance data */
} cip_object_instance_t;

/* ============================================================================
 * CIP Object Class
 * ============================================================================ */

/**
 * CIP Object Class
 *
 * Represents a CIP object class (e.g., Identity, Symbol, Connection Manager).
 * Each class has a set of service handlers and instance management functions.
 */
typedef struct cip_object_class_s {
    uint16_t class_id;      /* CIP Class ID (0x01, 0x02, 0x6B, etc.) */
    const char *class_name; /* Human-readable name */

    /* Service handler table - indexed by service code (0-255) */
    cip_service_handler_t service_handlers[256];

    /* Instance management callbacks */
    cip_object_instance_t *(*get_instance)(uint32_t instance_id, plc_context_t *plc);
    util_err_t (*create_instance)(uint32_t instance_id, plc_context_t *plc);
    void (*destroy_instance)(cip_object_instance_t *instance);

    /* Class-specific data (rarely used) */
    void *class_data;
} cip_object_class_t;

/* ============================================================================
 * CIP Object Registry
 * ============================================================================ */

/**
 * CIP Object Registry
 *
 * Maintains all registered CIP object classes.
 * Provides lookup and dispatch functionality.
 */
typedef struct {
    cip_object_class_t *classes[256]; /* Indexed by class ID */
    size_t class_count;
} cip_object_registry_t;

/* ============================================================================
 * Registry Functions
 * ============================================================================ */

/**
 * @brief Create an empty object registry
 *
 * Allocates and initializes a new registry.
 *
 * @return Newly allocated registry, or NULL on allocation failure
 */
cip_object_registry_t *cip_registry_create(void);

/**
 * @brief Destroy an object registry
 *
 * Frees the registry and all registered classes.
 *
 * @param registry Registry to destroy (may be NULL - no-op)
 */
void cip_registry_destroy(cip_object_registry_t *registry);

/**
 * @brief Register an object class
 *
 * Adds a class to the registry.
 *
 * @param registry Registry to register in
 * @param obj_class Class to register (will be stored, not copied)
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_registry_register_class(cip_object_registry_t *registry, cip_object_class_t *obj_class);

/**
 * @brief Look up object class by ID
 *
 * Finds a registered class by its class ID.
 *
 * @param registry Registry to search
 * @param class_id Class ID to look up
 * @return Class pointer, or NULL if not found
 */
cip_object_class_t *cip_registry_find_class(cip_object_registry_t *registry, uint16_t class_id);

/**
 * @brief Dispatch service to object
 *
 * Looks up class, gets instance, and dispatches to service handler.
 *
 * @param registry Registry to use
 * @param class_id Target class ID
 * @param instance_id Target instance ID
 * @param service Service code
 * @param path Parsed CIP path
 * @param request Request data buffer
 * @param response Response data buffer
 * @param plc PLC context
 * @return UTIL_OK on success, error code on failure
 */
util_err_t cip_registry_dispatch(cip_object_registry_t *registry, uint16_t class_id, uint32_t instance_id, uint8_t service,
                                 const cip_path_t *path, buf_t *request, buf_t *response, plc_context_t *plc);
