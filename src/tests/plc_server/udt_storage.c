#include "udt_storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Global counter for auto-assigning UDT IDs */
static uint16_t next_udt_id = 1;

/**
 * Create a new UDT definition with auto-assigned ID
 */
udt_def_t *udt_create(const char *name, size_t total_size) {
    if (!name || name[0] == '\0') {
        fprintf(stderr, "ERROR: UDT name cannot be empty\n");
        return NULL;
    }

    udt_def_t *udt = (udt_def_t *)malloc(sizeof(udt_def_t));
    if (!udt) {
        fprintf(stderr, "ERROR: Failed to allocate memory for UDT\n");
        return NULL;
    }

    /* Auto-assign ID */
    udt->udt_id = next_udt_id++;

    /* Copy name */
    strncpy(udt->name, name, sizeof(udt->name) - 1);
    udt->name[sizeof(udt->name) - 1] = '\0';

    /* Initialize members */
    udt->member_count = 0;
    udt->members = NULL;
    udt->total_size = total_size;
    udt->next = NULL;

    return udt;
}

/**
 * Add a member to a UDT
 */
int udt_add_member(udt_def_t *udt, const char *name, uint16_t symbol_type,
                   size_t byte_offset, size_t bit_offset, size_t element_length,
                   const uint32_t dimensions[3]) {
    if (!udt || !name || name[0] == '\0') {
        fprintf(stderr, "ERROR: Invalid UDT or member name\n");
        return -1;
    }

    /* Validate offsets */
    if (byte_offset >= udt->total_size) {
        fprintf(stderr, "ERROR: Member '%s' byte_offset (%zu) >= total_size (%zu)\n",
                name, byte_offset, udt->total_size);
        return -1;
    }

    if (bit_offset > 7) {
        fprintf(stderr, "ERROR: Member '%s' bit_offset (%zu) > 7\n", name, bit_offset);
        return -1;
    }

    /* Reallocate members array */
    udt_member_t *new_members = (udt_member_t *)realloc(udt->members,
                                                         (udt->member_count + 1) * sizeof(udt_member_t));
    if (!new_members) {
        fprintf(stderr, "ERROR: Failed to allocate memory for UDT member\n");
        return -1;
    }

    udt->members = new_members;

    /* Initialize new member */
    udt_member_t *member = &udt->members[udt->member_count];
    strncpy(member->name, name, sizeof(member->name) - 1);
    member->name[sizeof(member->name) - 1] = '\0';
    member->symbol_type = symbol_type;
    member->byte_offset = byte_offset;
    member->bit_offset = bit_offset;
    member->element_length = element_length;

    if (dimensions) {
        member->dimensions[0] = dimensions[0];
        member->dimensions[1] = dimensions[1];
        member->dimensions[2] = dimensions[2];
    } else {
        member->dimensions[0] = 0;
        member->dimensions[1] = 0;
        member->dimensions[2] = 0;
    }

    udt->member_count++;

    return 0;
}

/**
 * Find UDT by ID
 */
udt_def_t *udt_find_by_id(udt_def_t *head, uint16_t udt_id) {
    for (udt_def_t *udt = head; udt; udt = udt->next) {
        if (udt->udt_id == udt_id) {
            return udt;
        }
    }
    return NULL;
}

/**
 * Find UDT by name
 */
udt_def_t *udt_find_by_name(udt_def_t *head, const char *name) {
    if (!name) {
        return NULL;
    }

    for (udt_def_t *udt = head; udt; udt = udt->next) {
        if (strcmp(udt->name, name) == 0) {
            return udt;
        }
    }
    return NULL;
}

/**
 * Destroy entire UDT registry
 */
void udt_destroy_all(udt_def_t *head) {
    udt_def_t *current = head;

    while (current) {
        udt_def_t *next = current->next;

        /* Free members array */
        if (current->members) {
            free(current->members);
            current->members = NULL;
        }

        /* Free the UDT itself */
        free(current);

        current = next;
    }
}

/**
 * Get the next auto-assigned UDT ID
 */
uint16_t udt_get_next_id(void) {
    return next_udt_id;
}

/**
 * Reset UDT ID counter (for testing)
 */
void udt_reset_id_counter(void) {
    next_udt_id = 1;
}
