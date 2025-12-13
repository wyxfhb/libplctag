#pragma once

#include <stdint.h>
#include <stddef.h>
#include "err.h"

/* ============================================================================
 * Omron Type Definitions and Variable Storage
 *
 * This module manages the three-class Omron object model:
 * - Class 0x6A: Tag Name Server (variable directory)
 * - Class 0x6B: Variable Object (variable instances with data)
 * - Class 0x6C: Variable Type Object (structure definitions and members)
 *
 * All instance IDs are sequentially assigned:
 *   1-N:      Class 0x6B variable instances
 *   N+1-M:    Class 0x6C type definitions and members
 * ============================================================================ */

/* ============================================================================
 * Type Definition (Class 0x6C)
 * ============================================================================ */

/**
 * Omron Type Definition
 *
 * Represents a structure type definition (for Class 0x6C instances).
 * Forms a linked list in omron_registry_t->types.
 */
typedef struct omron_type_def_s {
    uint32_t type_instance_id;          /* Class 0x6C instance ID */
    char type_name[256];                /* Type name */
    uint16_t type_code;                 /* CIP type code (0xA0 for struct) */
    size_t total_size;                  /* Total size in bytes */
    size_t member_count;                /* Number of members */
    uint16_t crc_code;                  /* Structure checksum */
    uint32_t first_member_id;           /* Instance ID of first member (or 0) */
    struct omron_type_def_s *next;      /* Linked list next */
} omron_type_def_t;

/* ============================================================================
 * Type Member (also Class 0x6C instance)
 * ============================================================================ */

/**
 * Omron Type Member
 *
 * Represents one member of a structure type (also a Class 0x6C instance).
 * Members form a linked chain via next_member_id and Next Instance ID pointers.
 */
typedef struct omron_type_member_s {
    uint32_t member_instance_id;        /* Class 0x6C instance ID */
    char member_name[256];              /* Member name */
    uint16_t member_type;               /* CIP type code or nested type ID */
    size_t member_offset;               /* Offset within parent structure */
    size_t member_size;                 /* Size in bytes */
    uint32_t next_member_id;            /* Next sibling member (0=end) */
    uint32_t nesting_type_id;           /* If member is struct, type ID */
    struct omron_type_member_s *next;   /* Linked list next */
} omron_type_member_t;

/* ============================================================================
 * Variable Instance (Class 0x6B)
 * ============================================================================ */

/**
 * Omron Variable Instance
 *
 * Represents a variable in the PLC (Class 0x6B instance).
 * Can be scalar, array, or structure type.
 */
typedef struct omron_variable_s {
    uint32_t instance_id;               /* Class 0x6B instance ID */
    char name[256];                     /* Variable name */
    uint16_t type_code;                 /* CIP type code (primitive or 0xA0/0xA2) */
    uint32_t type_instance_id;          /* If struct, points to Class 0x6C type */
    size_t data_size;                   /* Total size in bytes */
    uint8_t *data;                      /* Variable data (malloc'd) */
    uint8_t dim_count;                  /* Array dimensions (0=scalar, 1-3=array) */
    uint32_t dimensions[3];             /* Dimension sizes */
    struct omron_variable_s *next;      /* Linked list next */
} omron_variable_t;

/* ============================================================================
 * Omron Registry
 * ============================================================================ */

/**
 * Omron Variable Registry
 *
 * Maintains all variables, types, and members for the Omron object model.
 * Stored in plc_context_t->omron_registry.
 */
typedef struct omron_registry_s {
    omron_variable_t *variables;        /* Linked list of variables */
    omron_type_def_t *types;            /* Linked list of type definitions */
    omron_type_member_t *members;       /* Linked list of type members */
    uint32_t next_var_instance_id;      /* Counter for variable IDs (starts at 1) */
    uint32_t next_type_instance_id;     /* Counter for type IDs */
} omron_registry_t;

/* ============================================================================
 * Registry Management Functions
 * ============================================================================ */

/**
 * @brief Create an empty Omron registry
 *
 * Allocates and initializes an empty registry. Counters start at 1.
 *
 * @return Newly allocated registry, or NULL on allocation failure
 */
omron_registry_t* omron_registry_create(void);

/**
 * @brief Destroy a registry and all contained data
 *
 * Frees all variables, types, members, and the registry itself.
 *
 * @param registry Registry to destroy (may be NULL - no-op)
 */
void omron_registry_destroy(omron_registry_t *registry);

/* ============================================================================
 * Variable Management Functions
 * ============================================================================ */

/**
 * @brief Create a scalar variable
 *
 * Allocates a scalar variable with auto-assigned instance ID.
 *
 * @param registry Registry to add variable to
 * @param name Variable name
 * @param type_code CIP type code (0xC3=INT, 0xC4=DINT, 0xCA=REAL, etc.)
 * @param size Element size in bytes (2 for INT, 4 for DINT/REAL, etc.)
 * @return Newly allocated variable, or NULL on failure
 */
omron_variable_t* omron_variable_create(omron_registry_t *registry,
                                         const char *name,
                                         uint16_t type_code,
                                         size_t size);

/**
 * @brief Create an array variable
 *
 * Allocates an array variable with specified element count.
 *
 * @param registry Registry to add variable to
 * @param name Variable name
 * @param type_code CIP type code of elements
 * @param elem_size Size of one element in bytes
 * @param elem_count Number of elements
 * @return Newly allocated variable, or NULL on failure
 */
omron_variable_t* omron_variable_create_array(omron_registry_t *registry,
                                               const char *name,
                                               uint16_t type_code,
                                               size_t elem_size,
                                               size_t elem_count);

/**
 * @brief Create a multi-dimensional array variable
 *
 * @param registry Registry to add variable to
 * @param name Variable name
 * @param type_code CIP type code of elements
 * @param elem_size Size of one element in bytes
 * @param dim_count Number of dimensions (1-3)
 * @param dimensions Array of dimension sizes
 * @return Newly allocated variable, or NULL on failure
 */
omron_variable_t* omron_variable_create_array_multi(omron_registry_t *registry,
                                                     const char *name,
                                                     uint16_t type_code,
                                                     size_t elem_size,
                                                     size_t dim_count,
                                                     const uint32_t *dimensions);

/**
 * @brief Create a structure variable
 *
 * Allocates a variable that is an instance of a user-defined type.
 *
 * @param registry Registry to add variable to
 * @param name Variable name
 * @param type_id Type instance ID (from omron_type_def_t)
 * @param type_def Type definition
 * @return Newly allocated variable, or NULL on failure
 */
omron_variable_t* omron_variable_create_struct(omron_registry_t *registry,
                                                const char *name,
                                                uint32_t type_id,
                                                omron_type_def_t *type_def);

/**
 * @brief Create a string variable
 *
 * Creates a variable with Omron string type (0xD0).
 * Format: 2-byte length + 82-byte character buffer (fixed 84 bytes total).
 *
 * @param registry Registry to add variable to
 * @param name Variable name
 * @param initial_value Initial string value (NULL for empty)
 * @return Newly allocated variable, or NULL on failure
 */
omron_variable_t* omron_variable_create_string(omron_registry_t *registry,
                                                const char *name,
                                                const char *initial_value);

/**
 * @brief Find variable by instance ID
 *
 * @param registry Registry to search
 * @param instance_id Variable instance ID
 * @return Variable pointer, or NULL if not found
 */
omron_variable_t* omron_variable_find_by_id(omron_registry_t *registry,
                                             uint32_t instance_id);

/**
 * @brief Find variable by name
 *
 * @param registry Registry to search
 * @param name Variable name
 * @return Variable pointer, or NULL if not found
 */
omron_variable_t* omron_variable_find_by_name(omron_registry_t *registry,
                                               const char *name);

/**
 * @brief Destroy a variable
 *
 * Frees the variable and its data buffer.
 *
 * @param var Variable to destroy (may be NULL - no-op)
 */
void omron_variable_destroy(omron_variable_t *var);

/* ============================================================================
 * Type Definition Management Functions
 * ============================================================================ */

/**
 * @brief Create a type definition
 *
 * Allocates a structure type definition with auto-assigned instance ID.
 *
 * @param registry Registry to add type to
 * @param type_name Type name
 * @param total_size Total size in bytes
 * @return Newly allocated type definition, or NULL on failure
 */
omron_type_def_t* omron_type_create(omron_registry_t *registry,
                                     const char *type_name,
                                     size_t total_size);

/**
 * @brief Add a member to a type definition
 *
 * Creates and links a member to a type definition.
 *
 * @param registry Registry (for ID assignment)
 * @param parent_type Type definition to add member to
 * @param member_name Member name
 * @param member_type CIP type code or nested type ID
 * @param offset Offset within structure
 * @param size Size of member
 * @return Newly allocated member, or NULL on failure
 */
omron_type_member_t* omron_member_add(omron_registry_t *registry,
                                       omron_type_def_t *parent_type,
                                       const char *member_name,
                                       uint16_t member_type,
                                       size_t offset,
                                       size_t size);

/**
 * @brief Find type definition by instance ID
 *
 * @param registry Registry to search
 * @param type_instance_id Type instance ID
 * @return Type definition pointer, or NULL if not found
 */
omron_type_def_t* omron_type_find_by_id(omron_registry_t *registry,
                                         uint32_t type_instance_id);

/**
 * @brief Find type member by instance ID
 *
 * @param registry Registry to search
 * @param member_instance_id Member instance ID
 * @return Member definition pointer, or NULL if not found
 */
omron_type_member_t* omron_member_find_by_id(omron_registry_t *registry,
                                              uint32_t member_instance_id);

/**
 * @brief Find type member by name within a type
 *
 * @param registry Registry to search
 * @param parent_type Type definition to search within
 * @param member_name Member name
 * @return Member definition pointer, or NULL if not found
 */
omron_type_member_t* omron_member_find_by_name(omron_registry_t *registry,
                                                omron_type_def_t *parent_type,
                                                const char *member_name);

/**
 * @brief Destroy a type definition and all its members
 *
 * @param registry Registry containing the type
 * @param type_def Type to destroy (may be NULL - no-op)
 */
void omron_type_destroy(omron_registry_t *registry, omron_type_def_t *type_def);

/**
 * @brief Destroy a type member
 *
 * @param member Member to destroy (may be NULL - no-op)
 */
void omron_member_destroy(omron_type_member_t *member);
