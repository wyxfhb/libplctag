# PLC Server Design Document
## CIP Object Model Implementation using Event-Driven Architecture

**Project**: plc_server - Greenfield EtherNet/IP PLC Simulator  
**Created**: December 13, 2025  
**Architecture**: Event-driven with stackless coroutines (protothreads)  
**Model**: CIP Object-Oriented Architecture  

---

## Executive Summary

This document specifies the design and implementation of a **new** EtherNet/IP PLC simulator built from the ground up following the CIP (Common Industrial Protocol) object model specification. The architecture uses stackless coroutines (protothreads) for event-driven operation, mirroring the proven design of the modbus_server.

**Core Design Principles:**
1. **Specification-Driven**: Code structure mirrors CIP/EIP specification hierarchy
2. **Object-Oriented Protocol**: Implement CIP objects as first-class entities
3. **Event-Driven**: Single-threaded with coroutines, not threads-per-connection
4. **Declarative Configuration**: PLCs register objects/services they support
5. **Testable Components**: Each layer and object class independently testable

**Key Differences from ab_server:**
- **New codebase**: Not a refactoring, build from scratch
- **Object model**: CIP objects are explicit data structures with service handlers
- **Clean layering**: EIP → CPF → CIP Message Router → Object Classes
- **Protocol compliance**: Follows ODVA CIP specification organization
- **Event-driven**: Uses coro_net.h for coroutine networking (like modbus_server)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Protocol Layer Stack](#2-protocol-layer-stack)
3. [CIP Object Model](#3-cip-object-model)
4. [Core Object Classes](#4-core-object-classes)
5. [PLC-Specific Configuration](#5-plc-specific-configuration)
6. [Data Structures](#6-data-structures)
7. [Implementation Phases](#7-implementation-phases)
8. [File Organization](#8-file-organization)
9. [Testing Strategy](#9-testing-strategy)
10. [Reference Materials](#10-reference-materials)

---

## 1. Architecture Overview

### 1.1 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    plc_server.c                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              Event Loop (coro_net.c)                      │  │
│  │  - Stackless coroutines (protothreads pattern)            │  │
│  │  - Non-blocking I/O with edge-triggered polling           │  │
│  │  - Per-connection state machines                          │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │         Client Connection Coroutine                       │  │
│  │  PT_BEGIN()                                               │  │
│  │    1. PT_WAIT_UNTIL(recv EIP packet)                      │  │
│  │    2. Process: eip_dispatch()                             │  │
│  │    3. PT_WAIT_UNTIL(send response)                        │  │
│  │  PT_END()                                                 │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  eip_protocol.c                                 │
│  - Parse EIP encapsulation header (24 bytes)                   │
│  - Handle EIP commands:                                         │
│    • 0x0065: RegisterSession                                    │
│    • 0x0066: UnregisterSession                                  │
│    • 0x0004: ListServices                                       │
│    • 0x006F: SendRRData (Unconnected)                          │
│    • 0x0070: SendUnitData (Connected)                          │
│  - Route SendRRData/SendUnitData → cpf_dispatch()              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   cpf_protocol.c                                │
│  - Parse Common Packet Format (CPF)                            │
│  - Extract items:                                               │
│    • 0x0000: Null Address Item                                  │
│    • 0x00A1: Connected Address Item                            │
│    • 0x00B1: Connected Data Item                               │
│    • 0x00B2: Unconnected Data Item                            │
│  - Route to CIP Message Router                                  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│               cip_message_router.c                              │
│  (Implementation of CIP Class 0x02, Instance 1)                 │
│                                                                 │
│  - Parse CIP service byte                                       │
│  - Parse CIP path (Class/Instance/Attribute or Tag Name)        │
│  - Look up object class in registry                             │
│  - Dispatch to object's service handler                         │
└─────────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┬──────────────┐
              ▼                               ▼              ▼
┌─────────────────────┐    ┌──────────────────────┐   ┌────────────────┐
│  Class 0x01         │    │  Class 0x06          │   │ Class 0x6B     │
│  Identity Object    │    │  Connection Manager  │   │ Symbol Object  │
│  - Get Attributes   │    │  - Forward Open      │   │ (ControlLogix) │
│                     │    │  - Forward Close     │   │ - Read Tag     │
└─────────────────────┘    └──────────────────────┘   │ - Write Tag    │
                                                       │ - List Tags    │
                                                       └────────────────┘
```

### 1.2 Design Philosophy

**Following modbus_server Pattern:**
- Single-threaded event loop
- Coroutines for connection state machines
- buf_t for all buffer management
- Layered protocol handling

**Following CIP Specification:**
- Objects are first-class entities
- Services are object-specific operations
- Class ID and Instance ID addressing
- Standard error codes and status

**Unlike ab_server:**
- No threads, no mutexes
- No plc_type conditionals
- Objects register themselves
- Clean separation of protocol layers

---

## 2. Protocol Layer Stack

### 2.1 Layer 1: EIP Encapsulation

**File**: `eip_protocol.c` / `eip_protocol.h`

**Responsibilities:**
- Parse 24-byte EIP encapsulation header
- Handle session management (Register/Unregister)
- Route data commands to CPF layer
- Manage session handles

**EIP Packet Structure:**
```
[0-1]   uint16_le  Command (0x0065, 0x006F, 0x0070, etc.)
[2-3]   uint16_le  Length (of data following header)
[4-7]   uint32_le  Session Handle
[8-11]  uint32_le  Status
[12-19] uint64     Sender Context (echo back)
[20-23] uint32_le  Options (usually 0)
[24+]   uint8[]    Encapsulated Data
```

**Key Functions:**
```c
/* Parse EIP header from buffer */
util_err_t eip_parse_header(buf_t *input, eip_header_t *header);

/* Build EIP response header */
util_err_t eip_build_response_header(buf_t *output, const eip_header_t *req,
                                     uint32_t status, uint16_t data_length);

/* Main EIP dispatcher */
util_err_t eip_dispatch(buf_t *input, buf_t *output, 
                       eip_session_t *session, plc_context_t *plc);
```

**EIP Commands Handled:**
- `0x0004` ListServices - Return supported services
- `0x0063` ListIdentity - Return device identity information
- `0x0065` RegisterSession - Create new session
- `0x0066` UnregisterSession - Close session
- `0x006F` SendRRData - Unconnected message (most common)
- `0x0070` SendUnitData - Connected message (for I/O)

### 2.2 Layer 2: CPF (Common Packet Format)

**File**: `cpf_protocol.c` / `cpf_protocol.h`

**Responsibilities:**
- Parse CPF item list structure
- Extract address and data items
- Route CIP data to Message Router
- Build CPF response packets

**CPF Packet Structure:**
```
[0-1]   uint16_le  Item Count
[2-3]   uint16_le  Address Item Type ID
[4-5]   uint16_le  Address Item Length
[6+]    uint8[]    Address Item Data (if length > 0)
        uint16_le  Data Item Type ID
        uint16_le  Data Item Length
        uint8[]    Data Item Data (CIP message)
```

**Key Functions:**
```c
/* Parse CPF packet */
util_err_t cpf_parse_packet(buf_t *input, cpf_packet_t *packet);

/* Build CPF response */
util_err_t cpf_build_response(buf_t *output, const cpf_packet_t *request,
                              buf_t *cip_response);

/* Dispatch to CIP */
util_err_t cpf_dispatch(const cpf_packet_t *packet, buf_t *input,
                       buf_t *output, plc_context_t *plc);
```

**CPF Item Types:**
- `0x0000` Null Address - Unconnected messages
- `0x00A1` Connected Address - Connected messages (has connection ID)
- `0x00B1` Connected Data Item
- `0x00B2` Unconnected Data Item (most common for tag read/write)

### 2.3 Layer 3: CIP Message Router

**File**: `cip_message_router.c` / `cip_message_router.h`

**Responsibilities:**
- Parse CIP service byte
- Parse CIP path (EPATH format)
- Look up target object class and instance
- Dispatch to object service handler
- Build CIP response headers

**CIP Message Structure:**
```
[0]     uint8     Service Code (0x4C=Read, 0x4D=Write, etc.)
[1]     uint8     Path Size (in words)
[2+]    uint8[]   Path (sequence of EPATH segments)
        uint8[]   Service-specific data
```

**CIP Path Segment Types:**

1. **Logical Segment** (0x20-0x3F):
```
0x20 <class_id>        - 8-bit class ID
0x21 <class_id_16>     - 16-bit class ID
0x24 <instance_id>     - 8-bit instance ID
0x25 <instance_id_16>  - 16-bit instance ID
0x30 <attribute_id>    - 8-bit attribute ID (optional)
```

2. **Symbolic Segment** (0x91):
```
0x91 <length> <name_bytes> [padding]
```

3. **Data Segment** (0x80-0x9F) - Omron:
```
0x80 <offset_bytes> <element_count>  - Omron read/write with offset
```

4. **Example Paths:**
```
0x20 0x6B 0x24 0x01 0x91 <len> <tag_name>  - ControlLogix: Symbol Object + tag name
0x91 <len> <tag_name> 0x80 <offset> <count> - Omron: Tag name + data segment
0x20 0x01 0x24 0x01                         - Identity Object, Instance 1
```

**Key Functions:**
```c
/* Parse CIP service and path */
util_err_t cip_parse_request(buf_t *input, cip_request_t *request);

/* Main CIP dispatcher */
util_err_t cip_message_router_dispatch(const cip_request_t *request,
                                       buf_t *input, buf_t *output,
                                       plc_context_t *plc);

/* Build CIP response */
util_err_t cip_build_response(buf_t *output, uint8_t service,
                             uint8_t status, uint8_t *ext_status,
                             uint8_t ext_status_size);
```

---

## 3. CIP Object Model

### 3.1 Object Model Fundamentals

**CIP defines objects using an object-oriented model:**

- **Class**: A type of object (e.g., Identity Object, Symbol Object)
- **Instance**: A specific occurrence of a class (e.g., Identity Instance 1)
- **Attribute**: A property of an instance (e.g., Vendor ID, Product Name)
- **Service**: An operation on a class or instance (e.g., Get Attributes All, Read Tag)

### 3.2 Object Registry

**File**: `cip_object_registry.c` / `cip_object_registry.h`

The registry maintains all CIP object classes and provides lookup/dispatch.

```c
/**
 * CIP Object Class
 * 
 * Represents a CIP object class (e.g., Identity, Symbol, Connection Manager).
 * Each class has a set of service handlers for operations on that class.
 */
typedef struct cip_object_class_s {
    uint16_t class_id;              /* CIP Class ID (0x01, 0x02, 0x06, etc.) */
    const char *class_name;         /* Human-readable name */
    
    /* Service handler table - indexed by service code */
    cip_service_handler_t service_handlers[256];
    
    /* Instance management */
    cip_object_instance_t* (*get_instance)(uint32_t instance_id, 
                                           plc_context_t *plc);
    util_err_t (*create_instance)(uint32_t instance_id, 
                                  plc_context_t *plc);
    void (*destroy_instance)(cip_object_instance_t *instance);
    
    /* Class attributes (if any) */
    void *class_attributes;
} cip_object_class_t;

/**
 * CIP Object Instance
 * 
 * Represents a specific instance of a class.
 */
typedef struct cip_object_instance_s {
    cip_object_class_t *object_class;
    uint32_t instance_id;
    void *instance_data;            /* Class-specific data */
} cip_object_instance_t;

/**
 * Service Handler Signature
 * 
 * All CIP service handlers follow this signature.
 */
typedef util_err_t (*cip_service_handler_t)(
    uint8_t service,                /* Service code (0x4C, 0x4D, etc.) */
    const cip_path_t *path,         /* Parsed path (may have symbolic segment) */
    buf_t *request,                 /* Request data (after service+path) */
    buf_t *response,                /* Response buffer to write to */
    cip_object_instance_t *instance,/* Target instance */
    plc_context_t *plc              /* PLC context */
);

/**
 * Object Registry
 * 
 * Maintains all registered CIP object classes.
 */
typedef struct cip_object_registry_s {
    cip_object_class_t *classes[65536];  /* Indexed by class ID */
    size_t class_count;
} cip_object_registry_t;
```

**Registry Functions:**
```c
/* Initialize empty registry */
cip_object_registry_t* cip_registry_create(void);

/* Register an object class */
util_err_t cip_registry_register_class(cip_object_registry_t *registry,
                                       cip_object_class_t *obj_class);

/* Look up object class by ID */
cip_object_class_t* cip_registry_find_class(cip_object_registry_t *registry,
                                            uint16_t class_id);

/* Dispatch to object service handler */
util_err_t cip_registry_dispatch(cip_object_registry_t *registry,
                                uint16_t class_id, uint32_t instance_id,
                                uint8_t service, const cip_path_t *path,
                                buf_t *request, buf_t *response,
                                plc_context_t *plc);
```

### 3.3 Path Parsing

**File**: `cip_path.c` / `cip_path.h`

CIP paths are sequences of segments. Each segment has a type and type-specific data.

```c
/**
 * CIP Path Segment Types
 * 
 * These match the CIP specification segment formats. The type encodes
 * both the logical meaning and the size of the data.
 */
typedef enum {
    /* Logical Class segment */
    CIP_SEGMENT_LOGICAL_CLASS_8BIT          = 0x20, /* 0x20 - Class ID (8-bit) */
    CIP_SEGMENT_LOGICAL_CLASS_16BIT         = 0x21, /* 0x21 - Class ID (16-bit) */
    CIP_SEGMENT_LOGICAL_CLASS_32BIT         = 0x22, /* 0x22 - Class ID (32-bit) */

    /* Logical Instance segment */
    CIP_SEGMENT_LOGICAL_INSTANCE_8BIT       = 0x24, /* 0x24 - Instance ID (8-bit) */
    CIP_SEGMENT_LOGICAL_INSTANCE_16BIT      = 0x25, /* 0x25 - Instance ID (16-bit) */
    CIP_SEGMENT_LOGICAL_INSTANCE_32BIT      = 0x26, /* 0x26 - Instance ID (32-bit) */

    /* Logical Attribue segment */
    CIP_SEGMENT_LOGICAL_ATTRIBUTE_8BIT      = 0x30, /* 0x30 - Attribute ID (8-bit) */
    CIP_SEGMENT_LOGICAL_ATTRIBUTE_16BIT     = 0x31, /* 0x31 - Attribute ID (16-bit) */
    CIP_SEGMENT_LOGICAL_ATTRIBUTE_32BIT     = 0x32, /* 0x32 - Attribute ID (32-bit, rare) */

    /* Numeric segments - for array indexes */
    CIP_SEGMENT_NUMERIC_8BIT                = 0x28, /* 0x28 - Numeric (8-bit) */
    CIP_SEGMENT_NUMERIC_16BIT               = 0x29, /* 0x29 - Numeric (16-bit) */
    CIP_SEGMENT_NUMERIC_32BIT               = 0x2A, /* 0x2A - Numeric (32-bit) */
    
    /* Other segment types */
    CIP_SEGMENT_EXT_SYMBOLIC                = 0x91, /* 0x91 - ANSI Extended Symbolic */
    CIP_SEGMENT_DATA                        = 0x80, /* 0x80 - Data segment (Omron) */

    /* Port/link segment types */
    CIP_SEGMENT_PORT_LINK                   = 0x00, /* Normal Port/link */ 
    CIP_SEGMENT_PORT_EXT_LINK               = 0x10, /* port + extended link */
} cip_segment_type_t;

/**
 * CIP Path Segment
 * 
 * Union of all possible segment types. The type field indicates which
 * union member is valid and encodes the segment format/size.
 */
typedef struct cip_segment_s {
    cip_segment_type_t type;
    
    union {
        /* Logical segments - ID stored as uint32_t, 
         * actual size determined by segment type */
        struct {
            uint32_t id;              /* Class, Instance, Attribute, or Connection ID */
        } logical;
        
        /* Symbolic segment (tag name) */
        struct {
            const char *name;         /* Points into parsed buffer */
            size_t length;            /* Name length in bytes */
        } symbolic;
        
        /* Data segment (Omron-style) */
        struct {
            uint32_t offset_bytes;    /* Byte offset for read/write */
            uint16_t element_count;   /* Number of elements */
        } data;
        
        /* Port segment - simple format (CIP_SEGMENT_PORT_LINK)
         * Format: Single byte port number, single byte link address
         * Example: Port 1, Link 0 (backplane slot 0) = 0x01, 0x00
         * Example: Port 2, Link 5 (DH+ node 5) = 0x02, 0x05
         * 
         * Link address interpretation depends on module type in the port:
         *   - Backplane: link address = slot number (0-15 typical)
         *   - DH+ (DHRIO module): link address = DH+ node ID
         *   - ControlNet: link address = ControlNet node ID
         * 
         * Note: Link addresses can go up to 16 or higher depending on the
         * network type. Extended encoding for link addresses >15 is not
         * well documented - may use extended format or simply allow byte
         * values 0-255.
         */
        struct {
            uint8_t port_number;      /* Port number (1-based typically) */
            uint8_t link_address;     /* Link address (interpretation depends on port/module) */
        } port_link;
        
        /* Port segment - extended link format (CIP_SEGMENT_PORT_EXT_LINK)
         * Format: Port byte (port | 0x10), length byte, string data
         * Example: Port 2 with IP "127.0.0.1:44818"
         *   Port byte: 0x12 (2 | 16) - extended format on port 2
         *   Length: 0x0F (15 bytes)
         *   String: "127.0.0.1:44818" (padded to even boundary if needed)
         * Port values: 18 (0x12) = port 2/A, 19 (0x13) = port 3/B
         */
        struct {
            uint8_t port_number;      /* Port number (with 0x10 flag set) */
            const char *address_string; /* IP:port string */
            size_t address_length;    /* Length of address string */
        } port_ext_link;
        
        /* Numeric segment (array index) - stored as uint32_t,
         * actual size determined by segment type */
        struct {
            uint32_t value;           /* Array index or numeric value */
        } numeric;
    };
} cip_segment_t;

/**
 * CIP Path
 * 
 * A parsed CIP path consists of an array of segments.
 */
typedef struct cip_path_s {
    cip_segment_t segments[16];       /* Max segments in a path */
    size_t segment_count;
    
    /* Cached lookups for common patterns */
    uint16_t class_id;                /* 0 if not present */
    uint32_t instance_id;             /* 0 if not present */
    const char *symbol_name;          /* NULL if not present */
    size_t symbol_length;
    uint32_t offset_bytes;            /* 0 if no data segment */
    uint16_t element_count;           /* 0 if no data segment */
} cip_path_t;

/* Parse EPATH from buffer */
util_err_t cip_parse_path(buf_t *input, uint8_t path_size_words, 
                         cip_path_t *path);

/* Build EPATH into buffer */
util_err_t cip_build_path(buf_t *output, const cip_path_t *path);

/* Helper: Find segment by type */
cip_segment_t* cip_path_find_segment(cip_path_t *path, cip_segment_type_t type);
```

---

## 4. Core Object Classes

### 4.1 Identity Object (Class 0x01)

**Specification**: ODVA CIP Vol 1, Chapter 5-1  
**Purpose**: Provides device identification information  
**Instances**: Instance 1 (always present)  

**File**: `objects/identity_object.c` / `objects/identity_object.h`

**Attributes (Instance 1):**
- Attribute 1: Vendor ID (uint16)
- Attribute 2: Device Type (uint16)
- Attribute 3: Product Code (uint16)
- Attribute 4: Revision (Major.Minor)
- Attribute 5: Status (uint16)
- Attribute 6: Serial Number (uint32)
- Attribute 7: Product Name (SHORT_STRING)

**Services:**
- `0x01` Get Attributes All - Return all attributes
- `0x0E` Get Attribute Single - Return one attribute

**Implementation:**
```c
typedef struct identity_instance_data_s {
    uint16_t vendor_id;          /* 1 = Rockwell, 1337 = Custom */
    uint16_t device_type;        /* 12 = Communications Adapter */
    uint16_t product_code;       /* Product identifier */
    uint8_t revision_major;
    uint8_t revision_minor;
    uint16_t status;
    uint32_t serial_number;
    char product_name[32];
} identity_instance_data_t;

/* Service 0x01: Get Attributes All */
util_err_t identity_service_get_attr_all(
    uint8_t service,
    const cip_path_t *path,
    buf_t *request,
    buf_t *response,
    cip_object_instance_t *instance,
    plc_context_t *plc
) {
    identity_instance_data_t *data = instance->instance_data;
    
    /* Build response: all attributes in order */
    buf_write_u16_le(response, "vendor_id", data->vendor_id);
    buf_write_u16_le(response, "device_type", data->device_type);
    buf_write_u16_le(response, "product_code", data->product_code);
    buf_write_u8(response, "revision_major", data->revision_major);
    buf_write_u8(response, "revision_minor", data->revision_minor);
    buf_write_u16_le(response, "status", data->status);
    buf_write_u32_le(response, "serial_number", data->serial_number);
    /* ... product name as SHORT_STRING ... */
    
    return UTIL_OK;
}

/* Registration function */
void identity_object_register(cip_object_registry_t *registry,
                             plc_context_t *plc) {
    cip_object_class_t *obj_class = calloc(1, sizeof(*obj_class));
    obj_class->class_id = 0x01;
    obj_class->class_name = "Identity Object";
    
    /* Register service handlers */
    obj_class->service_handlers[0x01] = identity_service_get_attr_all;
    obj_class->service_handlers[0x0E] = identity_service_get_attr_single;
    
    obj_class->get_instance = identity_get_instance;
    
    cip_registry_register_class(registry, obj_class);
}
```

### 4.2 Message Router Object (Class 0x02)

**Specification**: ODVA CIP Vol 1, Chapter 5-2  
**Purpose**: Routes CIP messages to target objects  
**Instances**: Instance 1 (always present)  

**File**: `objects/message_router_object.c`

This is a special object - it IS the message router itself. It handles:
- Multi-request service (0x0A) - Execute multiple services in one request
- Error responses
- Routing to other objects

**Services:**
- `0x0A` Multiple Service Packet - Execute array of sub-requests

**Implementation:**
```c
/* Service 0x0A: Multiple Service Packet */
util_err_t message_router_service_multi_request(
    uint8_t service,
    const cip_path_t *path,
    buf_t *request,
    buf_t *response,
    cip_object_instance_t *instance,
    plc_context_t *plc
) {
    /* Parse multi-request structure */
    uint16_t service_count;
    if (!buf_read_u16_le(request, "service_count", &service_count)) {
        return UTIL_EINVAL;
    }
    
    /* Read offset array */
    uint16_t offsets[service_count];
    for (uint16_t i = 0; i < service_count; i++) {
        if (!buf_read_u16_le(request, "offset", &offsets[i])) {
            return UTIL_EINVAL;
        }
    }
    
    /* Build response header */
    buf_write_u16_le(response, "service_count", service_count);
    
    /* Reserve space for response offsets (fill in later) */
    size_t offset_pos = buf_write_pos(response);
    for (uint16_t i = 0; i < service_count; i++) {
        buf_write_u16_le(response, "offset_placeholder", 0);
    }
    
    /* Process each sub-request */
    bool any_failed = false;
    for (uint16_t i = 0; i < service_count; i++) {
        /* Create sub-request buffer at offset */
        buf_t sub_request = buf_create_view(request, offsets[i], ...);
        buf_t sub_response = buf_create(...);
        
        /* Parse and dispatch sub-request */
        cip_request_t sub_req;
        cip_parse_request(&sub_request, &sub_req);
        
        util_err_t err = cip_message_router_dispatch(&sub_req, &sub_request,
                                                     &sub_response, plc);
        
        if (err != UTIL_OK) {
            any_failed = true;
        }
        
        /* Write offset to this sub-response */
        size_t current_offset = buf_write_pos(response) - offset_pos - 2;
        buf_set_u16_le(response, offset_pos + i * 2, current_offset);
        
        /* Append sub-response */
        buf_append(response, &sub_response);
    }
    
    /* Set overall status in CIP response header */
    /* Status 0x1E = "Service Error" if any sub-request failed */
    return any_failed ? CIP_STATUS_SERVICE_ERROR : UTIL_OK;
}
```

### 4.3 Connection Manager Object (Class 0x06)

**Specification**: ODVA CIP Vol 1, Chapter 3-5  
**Purpose**: Manages I/O connections (Forward Open/Close)  
**Instances**: Instance 1  

**File**: `objects/connection_manager_object.c`

**Services:**
- `0x54` Forward Open - Establish connection
- `0x5B` Large Forward Open - Establish large connection
- `0x4E` Forward Close - Close connection

**Implementation:**
```c
/* Service 0x54: Forward Open */
util_err_t connection_manager_service_forward_open(
    uint8_t service,
    const cip_path_t *path,
    buf_t *request,
    buf_t *response,
    cip_object_instance_t *instance,
    plc_context_t *plc
) {
    /* Parse Forward Open request */
    forward_open_request_t fo_req;
    if (!parse_forward_open_request(request, &fo_req)) {
        return UTIL_EINVAL;
    }
    
    /* Validate connection path matches this PLC */
    if (!validate_connection_path(&fo_req.conn_path, plc)) {
        return CIP_ERR_PATH_DEST_UNKNOWN;
    }
    
    /* Allocate connection ID */
    uint32_t conn_id = allocate_connection_id(plc);
    
    /* Store connection parameters */
    store_connection(plc, conn_id, &fo_req);
    
    /* Build Forward Open Response */
    buf_write_u32_le(response, "o_to_t_conn_id", conn_id);
    buf_write_u32_le(response, "t_to_o_conn_id", fo_req.o_to_t_conn_id);
    /* ... other response fields ... */
    
    return UTIL_OK;
}
```

### 4.4 Symbol Object (Class 0x6B) - ControlLogix

**Specification**: Rockwell Automation (proprietary)  
**Purpose**: Tag-based data access  
**Instances**: One per tag  

**File**: `objects/symbol_object_controllogix.c`

**Services:**
- `0x4C` Read Tag Service
- `0x4D` Write Tag Service
- `0x52` Read Tag Fragmented Service
- `0x53` Write Tag Fragmented Service
- `0x55` Read Tag Fragmented Service (Get Instance Attribute List)

**Implementation Example:**
```c
/* Service 0x4C: Read Tag */
util_err_t symbol_service_0x4c_read_tag(
    uint8_t service,
    const cip_path_t *path,
    buf_t *request,
    buf_t *response,
    cip_object_instance_t *instance,
    plc_context_t *plc
) {
    /* Parse request: element_count [offset_bytes for 0x52] */
    uint16_t element_count;
    uint32_t offset_bytes = 0;
    
    if (!buf_read_u16_le(request, "element_count", &element_count)) {
        return UTIL_EINVAL;
    }
    
    if (service == 0x52) {  /* Fragmented read */
        if (!buf_read_u32_le(request, "offset_bytes", &offset_bytes)) {
            return UTIL_EINVAL;
        }
    }
    
    /* Look up tag by name from path */
    tag_def_t *tag = find_tag_by_name(plc, path->symbol_name);
    if (!tag) {
        return CIP_ERR_PATH_DEST_UNKNOWN;
    }
    
    /* Calculate byte range to read */
    size_t start_offset = calculate_offset(tag, path->indexes, 
                                          path->num_indexes);
    start_offset += offset_bytes;
    
    size_t bytes_to_read = element_count * tag->elem_size;
    
    /* Write response header: tag type */
    buf_write_u16_le(response, "tag_type", tag->tag_type);
    
    /* Get write space for data */
    size_t available = buf_write_size(response);
    size_t actual_bytes = (bytes_to_read <= available) ? 
                          bytes_to_read : available;
    
    /* Copy tag data */
    buf_write_bytes(response, "data", tag->data + start_offset, 
                   actual_bytes);
    
    /* If data didn't fit, set status to 0x06 (partial) */
    if (actual_bytes < bytes_to_read) {
        /* Caller will set CIP status to 0x06 */
        return CIP_STATUS_PARTIAL_TRANSFER;
    }
    
    return UTIL_OK;
}
```

---

## 5. PLC-Specific Configuration

### 5.1 PLC Types

Each PLC type registers different objects and configures different behaviors.

**File**: `plc_types/controllogix.c`

```c
void controllogix_configure(plc_context_t *plc) {
    /* Register standard objects */
    identity_object_register(plc->registry, plc);
    message_router_object_register(plc->registry, plc);
    connection_manager_object_register(plc->registry, plc);
    
    /* Register ControlLogix-specific Symbol Object (Class 0x6B) */
    symbol_object_controllogix_register(plc->registry, plc);
    
    /* Configure identity */
    plc->identity.vendor_id = 1;  /* Rockwell */
    plc->identity.device_type = 12;  /* Communications Adapter */
    plc->identity.product_code = 0x0100;  /* Simulated ControlLogix */
    strcpy(plc->identity.product_name, "Simulated ControlLogix");
    
    /* ControlLogix requires connection path (port/slot) */
    plc->requires_connection_path = true;
    
    /* ControlLogix supports multi-request */
    plc->supports_multi_request = true;
}
```

**File**: `plc_types/omron.c`

```c
void omron_configure(plc_context_t *plc) {
    /* Register standard objects */
    identity_object_register(plc->registry, plc);
    message_router_object_register(plc->registry, plc);
    connection_manager_object_register(plc->registry, plc);
    
    /* Register Omron-specific objects */
    symbol_object_omron_register(plc->registry, plc);  /* Class 0x6A */
    type_object_omron_register(plc->registry, plc);    /* Class 0x6C */
    
    /* Configure identity */
    plc->identity.vendor_id = 0x0000;  /* Omron */
    plc->identity.device_type = 12;
    plc->identity.product_code = 0x0200;  /* Simulated Omron */
    strcpy(plc->identity.product_name, "Simulated Omron NJ");
    
    /* Omron does NOT require connection path */
    plc->requires_connection_path = false;
    
    /* Omron supports multi-request */
    plc->supports_multi_request = true;
}
```

**File**: `plc_types/micro800.c`

```c
void micro800_configure(plc_context_t *plc) {
    /* Register standard objects */
    identity_object_register(plc->registry, plc);
    message_router_object_register(plc->registry, plc);
    connection_manager_object_register(plc->registry, plc);
    
    /* Use ControlLogix Symbol Object (same tag structure) */
    symbol_object_controllogix_register(plc->registry, plc);
    
    /* Configure identity */
    plc->identity.vendor_id = 1;  /* Rockwell */
    plc->identity.device_type = 12;
    plc->identity.product_code = 0x0300;  /* Simulated Micro800 */
    strcpy(plc->identity.product_name, "Simulated Micro800");
    
    /* Micro800 does NOT require connection path */
    plc->requires_connection_path = false;
    
    /* Micro800 does NOT support multi-request */
    plc->supports_multi_request = false;
}
```

### 5.2 PLC Context Structure

```c
typedef struct plc_context_s {
    /* Configuration */
    const char *plc_type_name;
    bool requires_connection_path;
    bool supports_multi_request;
    
    /* Object registry */
    cip_object_registry_t *registry;
    
    /* Identity information */
    identity_instance_data_t identity;
    
    /* Connection management */
    connection_t *connections;
    uint32_t next_connection_id;
    
    /* Session management */
    uint32_t session_handle;
    
    /* Tag storage */
    tag_def_t *tags;
    size_t tag_count;
    
    /* Statistics */
    server_stats_t stats;
} plc_context_t;
```

---

## 6. Data Structures

### 6.1 Buffer Management

All I/O uses `buf_t` from `utils/buf.h`:

```c
typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    util_err_t error;
} buf_t;
```

### 6.2 Tag Storage

```c
typedef struct tag_def_s {
    char name[256];
    uint16_t tag_type;           /* CIP type code */
    size_t elem_size;            /* Size of one element in bytes */
    size_t elem_count;           /* Total elements */
    size_t num_dimensions;       /* 0=scalar, 1-3=array */
    size_t dimensions[3];        /* Array dimensions */
    uint8_t *data;               /* Tag data */
    uint32_t instance_id;        /* For tag enumeration */
    struct tag_def_s *next;
} tag_def_t;
```

### 6.3 Connection Storage

```c
typedef struct connection_s {
    uint32_t connection_id;
    uint32_t o_to_t_conn_id;     /* Originator to target */
    uint32_t t_to_o_conn_id;     /* Target to originator */
    uint16_t conn_serial_number;
    uint16_t vendor_id;
    uint32_t serial_number;
    uint32_t o_to_t_rpi;         /* Requested Packet Interval (us) */
    uint32_t t_to_o_rpi;
    bool active;
    int64_t last_activity_us;
    struct connection_s *next;
} connection_t;
```

---

## 7. Implementation Phases

### Phase 1: Core Infrastructure (Week 1)

**Deliverables:**
- Event loop with coro_net integration
- Basic client connection handling
- buf_t buffer management throughout
- Logging infrastructure

**Files to Create:**
- `plc_server.c` - Main server with event loop
- `plc_context.c/h` - PLC context management
- `tag_storage.c/h` - Tag data storage

**Success Criteria:**
- Server accepts connections
- Can receive and echo data
- Clean shutdown on signal

### Phase 2: EIP Layer (Week 2)

**Deliverables:**
- EIP encapsulation header parsing
- Session management (Register/Unregister)
- ListServices handler
- Basic SendRRData routing

**Files to Create:**
- `eip_protocol.c/h`
- `eip_session.c/h`

**Testing:**
- Send RegisterSession → get session handle
- Send ListServices → get service list
- Send SendRRData → forward to CPF layer

### Phase 3: CPF Layer (Week 2)

**Deliverables:**
- CPF packet parsing
- Item list extraction
- Route to CIP Message Router

**Files to Create:**
- `cpf_protocol.c/h`

**Testing:**
- Parse CPF with Null Address + Unconnected Data
- Extract CIP message
- Forward to Message Router

### Phase 4: CIP Message Router & Object Registry (Week 3)

**Deliverables:**
- CIP service and path parsing
- Object registry implementation
- Basic routing to object handlers

**Files to Create:**
- `cip_message_router.c/h`
- `cip_object_registry.c/h`
- `cip_path.c/h`

**Testing:**
- Parse CIP request
- Look up object by class ID
- Dispatch to service handler

### Phase 5: Core Objects (Week 3-4)

**Deliverables:**
- Identity Object (Class 0x01)
- Message Router Object (Class 0x02) - Multi-request
- Connection Manager Object (Class 0x06)

**Files to Create:**
- `objects/identity_object.c/h`
- `objects/message_router_object.c/h`
- `objects/connection_manager_object.c/h`

**Testing:**
- Get Identity attributes
- Execute multi-request
- Forward Open/Close

### Phase 6: ControlLogix Symbol Object (Week 4-5)

**Deliverables:**
- Symbol Object (Class 0x6B)
- Read Tag service (0x4C)
- Write Tag service (0x4D)
- Fragmented Read/Write (0x52, 0x53)
- List Tags service (0x55)

**Files to Create:**
- `objects/symbol_object_controllogix.c/h`

**Testing:**
- Read scalar tag
- Read array tag
- Read tag with fragmentation
- Write tag
- List all tags

### Phase 7: Additional PLC Types (Week 5-6)

**Deliverables:**
- Omron configuration
- Micro800 configuration
- PLC-specific object implementations

**Files to Create:**
- `plc_types/controllogix.c/h`
- `plc_types/omron.c/h`
- `plc_types/micro800.c/h`
- `objects/symbol_object_omron.c/h`
- `objects/type_object_omron.c/h`

**Testing:**
- All three PLC types functional
- Protocol differences validated
- Cross-PLC testing

---

## 8. File Organization

```
src/tests/plc_server/
├── plc_server.c                    - Main server, event loop
├── plc_context.c/h                 - PLC context management
├── tag_storage.c/h                 - Tag data storage
│
├── protocol/                       - Protocol layers
│   ├── eip_protocol.c/h            - EIP encapsulation
│   ├── eip_session.c/h             - Session management
│   ├── cpf_protocol.c/h            - Common Packet Format
│   ├── cip_message_router.c/h      - CIP Message Router
│   ├── cip_object_registry.c/h     - Object registry
│   └── cip_path.c/h                - Path parsing
│
├── objects/                        - CIP Object Classes
│   ├── identity_object.c/h         - Class 0x01
│   ├── message_router_object.c/h   - Class 0x02
│   ├── connection_manager_object.c/h - Class 0x06
│   ├── symbol_object_controllogix.c/h - Class 0x6B (ControlLogix)
│   ├── symbol_object_omron.c/h     - Class 0x6A (Omron)
│   └── type_object_omron.c/h       - Class 0x6C (Omron)
│
├── plc_types/                      - PLC-specific configuration
│   ├── controllogix.c/h            - ControlLogix setup
│   ├── omron.c/h                   - Omron setup
│   └── micro800.c/h                - Micro800 setup
│
├── tests/                          - Unit and integration tests
│   ├── test_eip_protocol.c
│   ├── test_cpf_protocol.c
│   ├── test_cip_path.c
│   ├── test_object_registry.c
│   └── test_symbol_object.c
│
└── CMakeLists.txt                  - Build configuration
```

---

## 9. Testing Strategy

### 9.1 Unit Tests

Each component has isolated unit tests:

```c
/* Example: test_cip_path.c */
void test_parse_logical_path(void) {
    /* Test: 0x20 0x01 0x24 0x01 (Identity, Instance 1) */
    uint8_t data[] = {0x20, 0x01, 0x24, 0x01};
    buf_t buf = buf_create_from_data(data, sizeof(data));
    
    cip_path_t path;
    util_err_t err = cip_parse_path(&buf, 2, &path);
    
    assert(err == UTIL_OK);
    assert(path.segment_count == 2);
    assert(path.segments[0].type == CIP_SEGMENT_LOGICAL_CLASS_8BIT);
    assert(path.segments[0].logical.id == 0x01);
    assert(path.segments[1].type == CIP_SEGMENT_LOGICAL_INSTANCE_8BIT);
    assert(path.segments[1].logical.id == 0x01);
    /* Cached lookups */
    assert(path.class_id == 0x01);
    assert(path.instance_id == 0x01);
}

void test_parse_symbolic_path(void) {
    /* Test: 0x91 0x07 "MyTag00" (padding) */
    uint8_t data[] = {0x91, 0x07, 'M', 'y', 'T', 'a', 'g', '0', '0', 0x00};
    buf_t buf = buf_create_from_data(data, sizeof(data));
    
    cip_path_t path;
    util_err_t err = cip_parse_path(&buf, 5, &path);
    
    assert(err == UTIL_OK);
    assert(path.segment_count == 1);
    assert(path.segments[0].type == CIP_SEGMENT_SYMBOLIC);
    assert(path.segments[0].symbolic.length == 7);
    assert(memcmp(path.segments[0].symbolic.name, "MyTag00", 7) == 0);
    /* Cached lookup */
    assert(path.symbol_name != NULL);
    assert(path.symbol_length == 7);
}

void test_parse_omron_path_with_data_segment(void) {
    /* Test: 0x91 0x06 "MyTag" 0x00 0x80 0x00 0x00 0x0A 0x00
     *       (Tag name + data segment with offset=0, count=10) */
    uint8_t data[] = {0x91, 0x06, 'M', 'y', 'T', 'a', 'g', 0x00,
                     0x80, 0x00, 0x00, 0x0A, 0x00};
    buf_t buf = buf_create_from_data(data, sizeof(data));
    
    cip_path_t path;
    util_err_t err = cip_parse_path(&buf, 6, &path);
    
    assert(err == UTIL_OK);
    assert(path.segment_count == 2);
    assert(path.segments[0].type == CIP_SEGMENT_SYMBOLIC);
    assert(path.segments[1].type == CIP_SEGMENT_DATA);
    assert(path.segments[1].data.offset_bytes == 0);
    assert(path.segments[1].data.element_count == 10);
    /* Cached lookups */
    assert(path.offset_bytes == 0);
    assert(path.element_count == 10);
}
```

### 9.2 Integration Tests

Test complete protocol stacks:

```c
/* Test: ControlLogix Read Tag */
void test_controllogix_read_tag(void) {
    /* Build complete EIP/CPF/CIP request */
    buf_t request = build_eip_packet(
        0x006F,  /* SendRRData */
        build_cpf_packet(
            0x00B2,  /* Unconnected Data Item */
            build_cip_request(
                0x4C,  /* Read Tag */
                build_symbolic_path("TestDINT"),
                build_read_request(1, 0)  /* Read 1 element, offset 0 */
            )
        )
    );
    
    /* Process through server */
    buf_t response = process_request(&request, plc);
    
    /* Validate response */
    assert(extract_cip_status(&response) == 0x00);  /* Success */
    assert(extract_tag_type(&response) == 0xC4);    /* DINT */
    assert(extract_data_length(&response) == 4);
}
```

### 9.3 Interoperability Testing

Test against real client libraries:
- libplctag
- pylogix
- pycomm3

```bash
# Test with libplctag
./bin_dist/plc_server --plc=controllogix --port=44818 &
plc_tag_dump protocol=ab_eip&gateway=127.0.0.1:44818&path=1,0&plc=controllogix&name=TestDINT

# Test with Python
python3 test_client.py --host=127.0.0.1 --port=44818 --plc=controllogix
```

---

## 10. Reference Materials

### 10.1 Specifications

- **ODVA CIP Vol 1**: Common Industrial Protocol (CIP) and the Family of CIP Networks
- **ODVA CIP Vol 2**: EtherNet/IP Adaptation of CIP
- **Rockwell Publication 1756-PM020**: ControlLogix Controllers Programming Manual

### 10.2 Existing Code References

**For Understanding Packet Formats:**
- `src/tests/ab_server/src/ethernet_ip/` - Existing packet parsing (reference only)
- `src/libplctag/protocols/ab/` - Client-side protocol implementation

**For Architecture Patterns:**
- `src/tests/modbus_server/` - Event-driven coroutine pattern (TEMPLATE)
- `src/tests/utils/` - Reusable utilities (buf.h, coro_net.h, etc.)

**NOT to Copy:**
- Do NOT copy ab_server's monolithic handlers
- Do NOT copy ab_server's thread-per-connection model
- Do NOT copy ab_server's mixed abstraction layers

### 10.3 Design Patterns

**Object Registry Pattern**: Registry of object classes with service handlers  
**Strategy Pattern**: PLC types configure different object sets  
**Chain of Responsibility**: EIP → CPF → CIP → Object dispatch  
**Coroutine Pattern**: State machines using protothreads (PT_BEGIN/PT_END)

---

## Appendix A: CIP Error Codes

```c
/* General CIP Error Codes */
#define CIP_OK                          0x00
#define CIP_ERR_CONNECTION_FAILURE      0x01
#define CIP_ERR_RESOURCE_UNAVAILABLE    0x02
#define CIP_ERR_INVALID_PARAM           0x03
#define CIP_ERR_PATH_SEGMENT            0x04
#define CIP_ERR_PATH_DEST_UNKNOWN       0x05
#define CIP_ERR_PARTIAL_TRANSFER        0x06
#define CIP_ERR_CONNECTION_LOST         0x07
#define CIP_ERR_SERVICE_NOT_SUPPORTED   0x08
#define CIP_ERR_INVALID_ATTRIBUTE       0x09
#define CIP_ERR_ATTR_LIST_ERROR         0x0A
#define CIP_ERR_ALREADY_IN_MODE         0x0B
#define CIP_ERR_OBJECT_STATE_CONFLICT   0x0C
#define CIP_ERR_OBJECT_ALREADY_EXISTS   0x0D
#define CIP_ERR_ATTR_NOT_SETTABLE       0x0E
#define CIP_ERR_PRIVILEGE_VIOLATION     0x0F
#define CIP_ERR_DEVICE_STATE_CONFLICT   0x10
#define CIP_ERR_REPLY_DATA_TOO_LARGE    0x11
#define CIP_ERR_FRAGMENTATION           0x12
#define CIP_ERR_NOT_ENOUGH_DATA         0x13
#define CIP_ERR_ATTR_NOT_SUPPORTED      0x14
#define CIP_ERR_TOO_MUCH_DATA           0x15
#define CIP_ERR_OBJECT_DOES_NOT_EXIST   0x16
#define CIP_ERR_SERVICE_FRAGMENTATION   0x17
#define CIP_ERR_NO_STORED_ATTR_DATA     0x18
#define CIP_ERR_STORE_OPERATION_FAILED  0x19
#define CIP_ERR_ROUTING_FAILURE         0x1A
#define CIP_ERR_ROUTING_FAILURE_BAD_SIZE 0x1B
#define CIP_ERR_ROUTING_FAILURE_BAD_ATTR 0x1C
#define CIP_ERR_TOO_MANY_PACKETS        0x1D
#define CIP_ERR_SERVICE_ERROR           0x1E  /* For multi-request */
```

## Appendix B: EIP Command Codes

```c
#define EIP_CMD_NOP                 0x0000
#define EIP_CMD_LIST_SERVICES       0x0004
#define EIP_CMD_LIST_IDENTITY       0x0063
#define EIP_CMD_LIST_INTERFACES     0x0064
#define EIP_CMD_REGISTER_SESSION    0x0065
#define EIP_CMD_UNREGISTER_SESSION  0x0066
#define EIP_CMD_SEND_RR_DATA        0x006F  /* Unconnected */
#define EIP_CMD_SEND_UNIT_DATA      0x0070  /* Connected */
```

## Appendix C: Common CIP Service Codes

```c
/* General Services (all objects) */
#define CIP_SRV_GET_ATTR_ALL        0x01
#define CIP_SRV_SET_ATTR_ALL        0x02
#define CIP_SRV_GET_ATTR_LIST       0x03
#define CIP_SRV_SET_ATTR_LIST       0x04
#define CIP_SRV_RESET               0x05
#define CIP_SRV_GET_ATTR_SINGLE     0x0E
#define CIP_SRV_SET_ATTR_SINGLE     0x10

/* Message Router Services */
#define CIP_SRV_MULTI_REQUEST       0x0A

/* Connection Manager Services */
#define CIP_SRV_FORWARD_CLOSE       0x4E
#define CIP_SRV_FORWARD_OPEN        0x54
#define CIP_SRV_LARGE_FORWARD_OPEN  0x5B

/* Symbol Object Services (ControlLogix) */
#define CIP_SRV_READ_TAG            0x4C
#define CIP_SRV_WRITE_TAG           0x4D
#define CIP_SRV_READ_TAG_FRAG       0x52
#define CIP_SRV_WRITE_TAG_FRAG      0x53
#define CIP_SRV_LIST_TAGS           0x55
#define CIP_SRV_GET_INSTANCE_LIST   0x5F
```

---

**Document Version**: 1.0  
**Status**: Design Phase  
**Next Steps**: Begin Phase 1 implementation

---

## Appendix D: Detailed Implementation Guide

### D.1 EIP Header Parsing - Complete Implementation

**EIP Header Structure (24 bytes fixed):**
```c
typedef struct eip_header_s {
    uint16_t command;           /* EIP command code */
    uint16_t length;            /* Length of data after header */
    uint32_t session_handle;    /* Session identifier */
    uint32_t status;            /* Response status code */
    uint64_t sender_context;    /* Echo back in response */
    uint32_t options;           /* Command-specific flags */
} eip_header_t;
```

**Parsing Algorithm:**
```c
util_err_t eip_parse_header(buf_t *input, eip_header_t *header) {
    /* Verify minimum size */
    if (buf_read_size(input) < 24) {
        return UTIL_EAGAIN;
    }
    
    /* Read all fields in little-endian byte order */
    if (!buf_read_u16_le(input, "command", &header->command)) {
        return UTIL_EINVAL;
    }
    if (!buf_read_u16_le(input, "length", &header->length)) {
        return UTIL_EINVAL;
    }
    if (!buf_read_u32_le(input, "session_handle", &header->session_handle)) {
        return UTIL_EINVAL;
    }
    if (!buf_read_u32_le(input, "status", &header->status)) {
        return UTIL_EINVAL;
    }
    if (!buf_read_u64_le(input, "sender_context", &header->sender_context)) {
        return UTIL_EINVAL;
    }
    if (!buf_read_u32_le(input, "options", &header->options)) {
        return UTIL_EINVAL;
    }
    
    /* Validate length doesn't exceed buffer */
    if (header->length > buf_read_size(input)) {
        return UTIL_EAGAIN;
    }
    
    return UTIL_OK;
}
```

**Building Response Header:**
```c
util_err_t eip_build_response_header(buf_t *output, 
                                     const eip_header_t *request,
                                     uint32_t status, 
                                     uint16_t data_length) {
    /* Echo command */
    buf_write_u16_le(output, "command", request->command);
    
    /* Write data length */
    buf_write_u16_le(output, "length", data_length);
    
    /* Echo session handle */
    buf_write_u32_le(output, "session_handle", request->session_handle);
    
    /* Write status */
    buf_write_u32_le(output, "status", status);
    
    /* Echo sender context */
    buf_write_u64_le(output, "sender_context", request->sender_context);
    
    /* Echo options */
    buf_write_u32_le(output, "options", request->options);
    
    return UTIL_OK;
}
```

### D.2 CIP Path Parsing - Complete Implementation

**Path Parsing Algorithm:**
```c
util_err_t cip_parse_path(buf_t *input, uint8_t path_size_words, 
                         cip_path_t *path) {
    size_t path_bytes = path_size_words * 2;  /* Convert words to bytes */
    size_t start_pos = buf_read_pos(input);
    size_t end_pos = start_pos + path_bytes;
    
    /* Initialize path */
    memset(path, 0, sizeof(*path));
    
    /* Parse segments until we reach end */
    while (buf_read_pos(input) < end_pos && path->segment_count < 16) {
        uint8_t segment_format;
        if (!buf_read_u8(input, "segment_format", &segment_format)) {
            return UTIL_EINVAL;
        }
        
        cip_segment_t *seg = &path->segments[path->segment_count];
        
        /* Determine segment type from format byte */
        switch (segment_format) {
            case CIP_SEGMENT_LOGICAL_CLASS_8BIT:
                seg->type = CIP_SEGMENT_LOGICAL_CLASS_8BIT;
                if (!buf_read_u8(input, "class_id", (uint8_t*)&seg->logical.id)) {
                    return UTIL_EINVAL;
                }
                path->class_id = (uint16_t)seg->logical.id;
                break;
                
            case CIP_SEGMENT_LOGICAL_CLASS_16BIT:
                seg->type = CIP_SEGMENT_LOGICAL_CLASS_16BIT;
                buf_read_u8(input, "padding", NULL); /* Skip padding byte */
                if (!buf_read_u16_le(input, "class_id", (uint16_t*)&seg->logical.id)) {
                    return UTIL_EINVAL;
                }
                path->class_id = (uint16_t)seg->logical.id;
                break;
                
            case CIP_SEGMENT_LOGICAL_INSTANCE_8BIT:
                seg->type = CIP_SEGMENT_LOGICAL_INSTANCE_8BIT;
                if (!buf_read_u8(input, "instance_id", (uint8_t*)&seg->logical.id)) {
                    return UTIL_EINVAL;
                }
                path->instance_id = seg->logical.id;
                break;
                
            case CIP_SEGMENT_LOGICAL_INSTANCE_16BIT:
                seg->type = CIP_SEGMENT_LOGICAL_INSTANCE_16BIT;
                buf_read_u8(input, "padding", NULL);
                if (!buf_read_u16_le(input, "instance_id", (uint16_t*)&seg->logical.id)) {
                    return UTIL_EINVAL;
                }
                path->instance_id = seg->logical.id;
                break;
                
            case CIP_SEGMENT_LOGICAL_ATTRIBUTE_8BIT:
                seg->type = CIP_SEGMENT_LOGICAL_ATTRIBUTE_8BIT;
                if (!buf_read_u8(input, "attribute_id", (uint8_t*)&seg->logical.id)) {
                    return UTIL_EINVAL;
                }
                break;
                
            case CIP_SEGMENT_NUMERIC_8BIT:
                seg->type = CIP_SEGMENT_NUMERIC_8BIT;
                if (!buf_read_u8(input, "value", (uint8_t*)&seg->numeric.value)) {
                    return UTIL_EINVAL;
                }
                break;
                
            case CIP_SEGMENT_NUMERIC_16BIT:
                seg->type = CIP_SEGMENT_NUMERIC_16BIT;
                buf_read_u8(input, "padding", NULL);
                if (!buf_read_u16_le(input, "value", (uint16_t*)&seg->numeric.value)) {
                    return UTIL_EINVAL;
                }
                break;
                
            case CIP_SEGMENT_EXT_SYMBOLIC:
                seg->type = CIP_SEGMENT_EXT_SYMBOLIC;
                uint8_t name_length;
                if (!buf_read_u8(input, "name_length", &name_length)) {
                    return UTIL_EINVAL;
                }
                
                /* Get pointer to name data in buffer */
                seg->symbolic.name = (const char*)buf_read_ptr(input);
                seg->symbolic.length = name_length;
                
                /* Cache for quick access */
                path->symbol_name = seg->symbolic.name;
                path->symbol_length = name_length;
                
                /* Advance past name */
                buf_read_skip(input, name_length);
                
                /* Pad to even boundary */
                if (name_length & 1) {
                    buf_read_skip(input, 1);
                }
                break;
                
            case CIP_SEGMENT_DATA:
                seg->type = CIP_SEGMENT_DATA;
                if (!buf_read_u32_le(input, "offset", &seg->data.offset_bytes)) {
                    return UTIL_EINVAL;
                }
                if (!buf_read_u16_le(input, "count", &seg->data.element_count)) {
                    return UTIL_EINVAL;
                }
                path->offset_bytes = seg->data.offset_bytes;
                path->element_count = seg->data.element_count;
                break;
                
            default:
                /* Check if it's a port segment */
                if ((segment_format & 0xF0) == 0x00 || 
                    (segment_format & 0xF0) == 0x10) {
                    /* Port segment */
                    uint8_t port_num = segment_format & 0x0F;
                    uint8_t link_addr;
                    
                    if (segment_format & 0x10) {
                        /* Extended link */
                        seg->type = CIP_SEGMENT_PORT_EXT_LINK;
                        seg->port_ext_link.port_number = port_num;
                        
                        /* Read length */
                        uint8_t addr_len;
                        if (!buf_read_u8(input, "addr_length", &addr_len)) {
                            return UTIL_EINVAL;
                        }
                        
                        /* Get pointer to address string */
                        seg->port_ext_link.address_string = (const char*)buf_read_ptr(input);
                        seg->port_ext_link.address_length = addr_len;
                        
                        /* Skip address and padding */
                        buf_read_skip(input, addr_len);
                        if (addr_len & 1) {
                            buf_read_skip(input, 1);
                        }
                    } else {
                        /* Simple link */
                        seg->type = CIP_SEGMENT_PORT_LINK;
                        seg->port_link.port_number = port_num;
                        if (!buf_read_u8(input, "link_addr", &link_addr)) {
                            return UTIL_EINVAL;
                        }
                        seg->port_link.link_address = link_addr;
                    }
                } else {
                    /* Unknown segment type */
                    return UTIL_ENOTSUPPORTED;
                }
                break;
        }
        
        path->segment_count++;
    }
    
    /* Verify we consumed exactly the right number of bytes */
    if (buf_read_pos(input) != end_pos) {
        return UTIL_EBOUNDS;
    }
    
    return UTIL_OK;
}
```

### D.3 Connection Handler Using coro_net.h

**Connection Context:**
```c
typedef struct client_context_s {
    coro_task_handle_t handle;
    plc_context_t *plc;
    
    /* Session state */
    uint32_t session_handle;
    
    /* I/O buffers */
    uint8_t recv_buffer[4096];
    uint8_t send_buffer[4096];
    buf_t recv_buf;
    buf_t send_buf;
    
    /* Statistics/timing */
    int64_t request_start_us;
    int64_t process_start_us;
} client_context_t;
```

**Frame Checking Function:**
```c
/* Returns UTIL_OK when we have a complete EIP frame, UTIL_EAGAIN otherwise */
util_err_t eip_frame_check(buf_t *buf, void *context) {
    (void)context;
    
    size_t available = buf_read_size(buf);
    
    /* Need at least EIP header (24 bytes) */
    if (available < 24) {
        return UTIL_EAGAIN;
    }
    
    /* Parse header to get data length */
    buf_t temp = *buf;  /* Copy for non-destructive read */
    uint16_t cmd, length;
    
    buf_read_u16_le(&temp, "command", &cmd);
    buf_read_u16_le(&temp, "length", &length);
    
    /* Check if we have complete packet (header + data) */
    if (available >= (24 + length)) {
        return UTIL_OK;
    }
    
    return UTIL_EAGAIN;
}
```

**Client Handler Coroutine:**
```c
static void client_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    client_context_t *client = (client_context_t *)context;
    util_err_t err;
    
    (void)fd;  /* fd is available via coro_get_fd(handle) if needed */
    
    CORO_START(handle);
    
    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client connected");
    
    while (1) {
        /* Compact receive buffer for next request */
        buf_compact(&client->recv_buf);
        
        /* Reset timing for new request */
        client->request_start_us = 0;
        
        /* Read until we get a complete EIP frame */
        socket_read_yield(handle, &client->recv_buf, eip_frame_check, NULL,
                         &client->request_start_us, NULL, err);
        
        if (err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client error during read: %s", 
                  util_err_str(err));
            break;
        }
        
        /* Process the request */
        client->process_start_us = util_time_us();
        buf_reset(&client->send_buf);
        
        err = eip_dispatch(&client->recv_buf, &client->send_buf, 
                          &client->session_handle, client->plc);
        
        if (err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Request processing failed: %s",
                  util_err_str(err));
            /* Send error response if possible */
        }
        
        /* Send response */
        socket_write_yield(handle, &client->send_buf, err);
        
        if (err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_WARN, "Client error during write: %s", 
                  util_err_str(err));
            break;
        }
        
        pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_DETAIL, "Request completed in %" PRId64 "us",
              util_time_us() - client->request_start_us);
    }
    
    pdlog(LOG_MODULE_PLC_CLIENT, LOG_LEVEL_INFO, "Client disconnected");
    
    /* Cleanup */
    coro_remove_task(client->handle);
    socket_close(coro_get_fd(client->handle));
    free(client);
    
    CORO_END(handle);
}
```

**Listener Handler:**
```c
static void listener_handler(coro_task_handle_t handle, socket_t fd, void *context) {
    plc_context_t *plc = (plc_context_t *)context;
    socket_t client_fd;
    socket_address_t client_addr;
    util_err_t err;
    
    (void)fd;
    
    CORO_START(handle);
    
    pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_INFO, "Listener started");
    
    while (plc->running) {
        /* Accept new connection (yields on EAGAIN) */
        socket_accept_yield(handle, &client_fd, &client_addr, err);
        
        if (err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_ERROR, "Accept failed: %s", 
                  util_err_str(err));
            break;
        }
        
        /* Log client info */
        char client_ip[INET6_ADDRSTRLEN];
        socket_address_get_addr_str(&client_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = socket_address_get_port(&client_addr);
        pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_INFO, 
              "Accepted connection from %s:%u (fd %d)", client_ip, client_port, client_fd);
        
        /* Create client context */
        client_context_t *client = (client_context_t *)calloc(1, sizeof(client_context_t));
        if (!client) {
            pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_ERROR, 
                  "Failed to allocate client context");
            CS_CLOSE(client_fd);
            continue;
        }
        
        client->plc = plc;
        client->recv_buf = buf_init(client->recv_buffer, sizeof(client->recv_buffer));
        client->send_buf = buf_init(client->send_buffer, sizeof(client->send_buffer));
        client->session_handle = 0;
        
        /* Add client task to event loop */
        err = coro_add_task(&client->handle, handle.coro_net, client_fd,
                           client_handler, client);
        if (err != UTIL_OK) {
            pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_ERROR, 
                  "Failed to add client task: %s", util_err_str(err));
            CS_CLOSE(client_fd);
            free(client);
            continue;
        }
    }
    
    pdlog(LOG_MODULE_PLC_LISTENER, LOG_LEVEL_INFO, "Listener stopped");
    
    CORO_END(handle);
}
```

### D.4 Complete Request/Response Examples

**Example 1: RegisterSession**

Request (hex):
```
65 00           Command: 0x0065
04 00           Length: 4
00 00 00 00     Session Handle: 0 (not assigned yet)
00 00 00 00     Status: 0
48 69 4D 6F 6D 00 00 00   Sender Context: "HiMom\0\0\0"
00 00 00 00     Options: 0
01 00           Protocol Version: 1
00 00           Options Flags: 0
```

Response (hex):
```
65 00           Command: 0x0065 (echo)
04 00           Length: 4
A3 B2 C1 D0     Session Handle: 0xD0C1B2A3 (assigned)
00 00 00 00     Status: 0 (success)
48 69 4D 6F 6D 00 00 00   Sender Context: (echoed)
00 00 00 00     Options: 0
01 00           Protocol Version: 1
00 00           Options Flags: 0
```

**Example 2: Read Tag "TestDINT"**

Full request packet (EIP + CPF + CIP):
```
[EIP Header - 24 bytes]
6F 00           Command: SendRRData (0x006F)
1E 00           Length: 30 bytes
A3 B2 C1 D0     Session Handle: 0xD0C1B2A3
00 00 00 00     Status: 0
48 69 4D 6F 6D 00 00 00   Sender Context
00 00 00 00     Options: 0

[CPF - 6 bytes]
00 00           Interface Handle: 0
00 00           Timeout: 0
02 00           Item Count: 2

[CPF Item 1: Null Address]
00 00           Type ID: 0x0000 (Null)
00 00           Length: 0

[CPF Item 2: Unconnected Data]
B2 00           Type ID: 0x00B2 (Unconnected Data)
0E 00           Length: 14 bytes

[CIP Request - 14 bytes]
4C              Service: 0x4C (Read Tag)
03              Path Size: 3 words (6 bytes)
91              Segment: 0x91 (Symbolic)
08              Name Length: 8
54 65 73 74 44 49 4E 54   Name: "TestDINT"
01 00           Element Count: 1
```

Response:
```
[EIP Header]
6F 00           Command: 0x006F (echo)
16 00           Length: 22 bytes
A3 B2 C1 D0     Session Handle: (echo)
00 00 00 00     Status: 0
48 69 4D 6F 6D 00 00 00   Sender Context: (echo)
00 00 00 00     Options: 0

[CPF]
00 00           Interface Handle: 0
00 00           Timeout: 0
02 00           Item Count: 2

[CPF Item 1: Null Address]
00 00           Type ID: 0x0000
00 00           Length: 0

[CPF Item 2: Unconnected Data]
B2 00           Type ID: 0x00B2
06 00           Length: 6 bytes

[CIP Response]
CC              Reply Service: 0xCC (0x4C | 0x80)
00              Reserved: 0
00              Status: 0x00 (success)
00              Extended Status Size: 0
C4 00           Tag Type: 0x00C4 (DINT)
2A 00 00 00     Data: 42 (0x0000002A in little-endian)
```

### D.5 Session Management Details

**Session Handle Generation:**
```c
uint32_t generate_session_handle(plc_context_t *plc) {
    /* Use simple counter, ensure non-zero */
    uint32_t handle;
    do {
        handle = ++plc->next_session_handle;
    } while (handle == 0);
    
    return handle;
}
```

**Session Storage:**
```c
typedef struct session_s {
    uint32_t handle;
    int64_t created_time_us;
    int64_t last_activity_us;
    bool active;
    struct session_s *next;
} session_t;

session_t* session_create(plc_context_t *plc, uint32_t handle) {
    session_t *session = calloc(1, sizeof(session_t));
    session->handle = handle;
    session->created_time_us = get_time_us();
    session->last_activity_us = session->created_time_us;
    session->active = true;
    
    /* Add to linked list */
    session->next = plc->sessions;
    plc->sessions = session;
    
    return session;
}

session_t* session_find(plc_context_t *plc, uint32_t handle) {
    for (session_t *s = plc->sessions; s != NULL; s = s->next) {
        if (s->handle == handle && s->active) {
            return s;
        }
    }
    return NULL;
}
```

### D.6 Memory Management Patterns

**Buffer Lifecycle:**
```c
/* Create buffer with capacity */
buf_t request_buf = buf_create(2048);

/* Use buffer... */
eip_parse_header(&request_buf, &header);

/* Reset for reuse */
buf_reset(&request_buf);

/* Destroy when done */
buf_destroy(&request_buf);
```

**Tag Data Storage:**
```c
tag_def_t* tag_create(const char *name, uint16_t type, size_t elem_size, 
                      size_t elem_count) {
    tag_def_t *tag = calloc(1, sizeof(tag_def_t));
    
    strncpy(tag->name, name, sizeof(tag->name) - 1);
    tag->tag_type = type;
    tag->elem_size = elem_size;
    tag->elem_count = elem_count;
    tag->num_dimensions = (elem_count > 1) ? 1 : 0;
    if (tag->num_dimensions > 0) {
        tag->dimensions[0] = elem_count;
    }
    
    /* Allocate data */
    tag->data = calloc(elem_count, elem_size);
    
    return tag;
}

void tag_destroy(tag_def_t *tag) {
    if (tag) {
        free(tag->data);
        free(tag);
    }
}
```

### D.7 Error Handling Patterns

**util_err_t to CIP Status Mapping:**
```c
uint8_t util_err_to_cip_status(util_err_t err) {
    switch (err) {
        case UTIL_OK:
            return CIP_OK;
        case UTIL_EINVAL:
            return CIP_ERR_INVALID_PARAM;
        case UTIL_ENOTFOUND:
            return CIP_ERR_PATH_DEST_UNKNOWN;
        case UTIL_EBOUNDS:
            return CIP_ERR_TOO_MUCH_DATA;
        case UTIL_ENOTSUPPORTED:
            return CIP_ERR_SERVICE_NOT_SUPPORTED;
        case UTIL_ERESOURCE:
            return CIP_ERR_RESOURCE_UNAVAILABLE;
        default:
            return CIP_ERR_CONNECTION_FAILURE;
    }
}
```

**Error Response Builder:**
```c
void cip_build_error_response(buf_t *output, uint8_t service,
                              uint8_t status, uint16_t extended_status) {
    /* Reply service = request service | 0x80 */
    buf_write_u8(output, "reply_service", service | 0x80);
    
    /* Reserved byte */
    buf_write_u8(output, "reserved", 0);
    
    /* General status */
    buf_write_u8(output, "status", status);
    
    /* Extended status size and data */
    if (extended_status != 0) {
        buf_write_u8(output, "ext_status_size", 2);
        buf_write_u16_le(output, "ext_status", extended_status);
    } else {
        buf_write_u8(output, "ext_status_size", 0);
    }
}
```

### D.8 Byte Order and Alignment Rules

**Critical Rules:**
1. **All multi-byte CIP fields**: Little-endian (Intel byte order)
2. **IP addresses in ListIdentity**: Big-endian (network byte order)
3. **Path sizes**: Always in WORDS (16-bit units), not bytes
4. **Symbolic segments**: Must pad to even byte boundary
5. **Port/link extended**: String data padded to even boundary

**Padding Example:**
```c
/* After writing variable-length data, pad to word boundary */
size_t bytes_written = buf_write_pos(output) - start_pos;
if (bytes_written & 1) {
    buf_write_u8(output, "padding", 0);
}
```

**Path Size Calculation:**
```c
/* Path size is always in 16-bit words */
size_t path_bytes = calculate_path_bytes(&path);
uint8_t path_size_words = (path_bytes + 1) / 2;  /* Round up */
buf_write_u8(output, "path_size", path_size_words);
```

---
