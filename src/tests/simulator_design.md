# Omron N-Series Simulator Design Document

## 1. Overview

This document outlines the design for a Python-based simulator of an Omron N-Series PLC (NJ/NX). The simulator will act as a server, allowing the `aphyt` client library to connect, discover variables (including complex nested structures), and perform read/write operations via EtherNet/IP (CIP over TCP).

## 2. Architecture

The simulator will be implemented as a single-threaded TCP server using Python's `selectors` module (or `select`) for I/O multiplexing. This avoids the complexity of thread synchronization while handling multiple client connections efficiently.

The protocol stack consists of three distinct layers:

1. **EIP Layer (Encapsulation):** Handles the outer protocol wrapper, session management, and command routing.
2. **CPF Layer (Common Packet Format):** Handles the packaging of CIP messages within EIP commands.
3. **CIP Layer (Common Industrial Protocol):** The application layer that implements the Object Model and processes specific services (Read, Write, Discovery).

## 3. Configuration and State Management

The simulator uses a two-file approach optimized for portability to C with minimal dependencies:

### 3.1. Configuration File (config.txt)

A simple text format defining structure types and variables. Easy to parse in both Python and C without external dependencies.

**Format Specification:**

```
# Lines starting with # are comments
# Blank lines are ignored

# Structure definitions
STRUCT MyPoint
  x INT
  y INT
END

STRUCT MyLine
  start MyPoint
  end MyPoint
END

# Variable declarations
VAR TestInt INT 42
VAR TestDint DINT -12345
VAR TestReal REAL 3.14159
VAR TestBool BOOL 1
VAR Point1 MyPoint 10,20
VAR Line1 MyLine 0,0,100,100
VAR _SystemFlag BOOL 0

# Array declarations
ARRAY TestArray INT[10] 0,1,2,3,4,5,6,7,8,9
ARRAY Points MyPoint[3] 0,0,10,10,20,20
```

**Syntax Rules:**

* **STRUCT** name: Begin structure definition
* **END**: End structure definition
* **VAR** name type initial_value: Declare a variable
* **ARRAY** name type[size] values: Declare an array
* Structure members: indented, format: `member_name type`
* Initial values for structures: comma-separated flattened member values
* System variables: prefix name with underscore `_`
* Types: BOOL, SINT, INT, DINT, REAL, or user-defined structure names

### 3.2. State File (state.bin)

A binary file storing current variable values using a fixed memory layout. This enables:
* Direct memory mapping (C: `mmap`, Python: `mmap`)
* Fast read/write without parsing overhead
* Zero external dependencies
* Easy portability to C

**Binary Format:**

```
Offset  Size   Field
------  ----   -----
0       4      Magic Number: 0x4F4D5254 ("OMRT" in ASCII)
4       4      Format Version: 1
8       4      Total Variables: N
12      4      Total Size: S bytes
16      4      Reserved
20      4      Reserved
24      ...    Variable Directory (N entries)
...     ...    Variable Data (S bytes)
```

**Variable Directory Entry (24 bytes each):**

```
Offset  Size   Field
------  ----   -----
0       4      Data Offset (from start of Variable Data section)
4       4      Data Size (in bytes)
8       4      Instance ID
12      4      CRC32 (of variable name)
16      8      Reserved
```

**Variable Data Section:**

Raw bytes for each variable, stored in declaration order:
* Scalars: stored as little-endian binary (INT=2 bytes, DINT=4 bytes, etc.)
* Structures: members stored sequentially, no padding
* Arrays: elements stored sequentially

**Example Memory Layout:**

```
Variable: TestInt (INT) = 42
  Offset in data section: 0
  Size: 2 bytes
  Bytes: 0x2A 0x00

Variable: Point1 (MyPoint) = {x:10, y:20}
  Offset in data section: 2
  Size: 4 bytes
  Bytes: 0x0A 0x00 0x14 0x00
         (x=10)   (y=20)
```

### 3.3. Startup Sequence

```
1. Parse config.txt
   - Build structure type definitions
   - Create variable instances
   - Assign Instance IDs (sequential: 1, 2, 3, ...)
   - Calculate memory offsets and sizes

2. Initialize variables with default values from config.txt

3. Check for state.bin:
   IF state.bin exists AND is valid:
     - Verify magic number and version
     - Verify variable count matches config
     - Load binary values into memory
   ELSE:
     - Use initial values from config.txt
     
4. Start server
```

### 3.4. Runtime State Persistence

**On Write Request:**
```
1. Update variable in memory
2. Calculate offset in state.bin
3. fseek() to offset
4. fwrite() the updated bytes
5. fflush() to disk
6. Return success response
```

**On Read Request:**
```
1. Read variable from memory (already in RAM)
2. Return value
(No disk I/O needed)
```

### 3.5. Implementation Notes

**Python Implementation:**
```python
import mmap
import struct

class StateFile:
    def __init__(self, filename, size):
        self.file = open(filename, 'r+b')
        self.mmap = mmap.mmap(self.file.fileno(), size)
    
    def write_int(self, offset, value):
        struct.pack_into('<h', self.mmap, offset, value)
        self.mmap.flush()
    
    def read_int(self, offset):
        return struct.unpack_from('<h', self.mmap, offset)[0]
```

**Future C Implementation:**
```c
#include <sys/mman.h>
#include <fcntl.h>

int fd = open("state.bin", O_RDWR);
char* mem = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

// Direct memory access
int16_t value = *(int16_t*)(mem + offset);
*(int16_t*)(mem + offset) = new_value;
msync(mem + offset, 2, MS_SYNC);
```

### 3.6. Command-Line Options

```bash
# Normal startup - restore from state
./simulator.py --config config.txt --state state.bin

# Reset to initial values (regenerate state.bin)
./simulator.py --config config.txt --reset

# Read-only mode (don't persist changes)
./simulator.py --config config.txt --readonly

# Dump state to text for debugging
./simulator.py --dump state.bin
```

## 4. Implementation Details

### 4.1. Network Layer (Single-Threaded I/O)

* **Mechanism:** Use the `selectors` module to monitor the listening socket and all active client sockets.
* **Event Loop:**
  * **Accept:** When the listening socket is ready, accept the new connection and register it with the selector.
  * **Read:** When a client socket is ready, read the incoming data.
  * **Process:** Pass the data through the protocol layers (EIP -> CPF -> CIP).
  * **Write:** Send the generated response back to the client.
* **State:** Since execution is single-threaded, the central data store (variables) does not require locking mechanisms.

### 4.2. Layer 1: EIP (Encapsulation)

The EIP layer processes the fixed 24-byte header and handles session lifecycle.

#### 4.2.1. EIP Header Format (24 bytes)

All EIP messages begin with a standardized 24-byte header:

```
Offset  Size  Field Name              Description
------  ----  ---------------------   -----------------------------------
0       2     Command                 EIP command code (little-endian)
2       2     Length                  Length of encapsulated data (little-endian)
4       4     Session Handle          Session identifier (little-endian)
8       4     Status                  Response code (little-endian)
12      8     Sender Context          Echoed back in response
20      4     Options                 Command-specific flags
24      ...   Command Specific Data   Variable length payload
```

**Command Codes:**

* `0x0004` - List Services
* `0x0063` - List Identity
* `0x0064` - List Interfaces
* `0x0065` - Register Session
* `0x0066` - Unregister Session
* `0x006F` - SendRRData (Send Request/Reply Data)

**Status Values:**

* `0x00000000` - Success
* `0x00000001` - Invalid/unsupported command
* `0x00000002` - Memory insufficient
* `0x00000003` - Data incorrect (malformed)
* `0x00000064` - Invalid session handle
* `0x00000065` - Version not supported

#### 4.2.2. Supported EIP Commands

##### List Services (0x0004)

**Request:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x0004
2       2     Length: 0x0000
4       4     Session: 0x00000000
8       4     Status: 0x00000000
12      8     Sender Context
20      4     Options: 0x00000000
```

**Response:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x0004
2       2     Length: (variable)
4       4     Session: (echoed)
8       4     Status: 0x00000000
12      8     Sender Context: (echoed)
20      4     Options: 0x00000000
24      ...   Item Count & Service Items
```

##### Register Session (0x0065)

**Request:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x0065
2       2     Length: 0x0004
4       4     Session: 0x00000000
8       4     Status: 0x00000000
12      8     Sender Context
20      4     Options: 0x00000000
24      2     Protocol Version: 0x0001
26      2     Options: 0x0000
```

**Response:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x0065
2       2     Length: 0x0004
4       4     Session: (new session handle, little-endian)
8       4     Status: 0x00000000 (success)
12      8     Sender Context: (echoed)
20      4     Options: 0x00000000
24      2     Protocol Version: 0x0001
26      2     Options: 0x0000
```

##### SendRRData (0x006F) - Encapsulated CIP Messages

**Request:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x006F
2       2     Length: (variable, CPF payload size)
4       4     Session: (registered handle)
8       4     Status: 0x00000000
12      8     Sender Context
20      4     Options: 0x00000000
24      4     Interface Handle: 0x00000000
28      2     Timeout: 0x0008 (8 seconds typical)
30      ...   Common Packet Format (CPF)
```

**Response:**
```
Offset  Size  Field
------  ----  -----------
0       2     Command: 0x006F
2       2     Length: (variable, CPF payload size)
4       4     Session: (echoed)
8       4     Status: 0x00000000
12      8     Sender Context: (echoed)
20      4     Options: 0x00000000
24      4     Interface Handle: 0x00000000
28      2     Timeout: 0x0008
30      ...   Common Packet Format (CPF) with CIP reply
```

### 4.3. Layer 2: CPF (Common Packet Format)

The CPF layer handles the wrapping of messages inside the `SendRRData` payload. It consists of an Item Count followed by a list of Address and Data items.

#### 4.3.1. CPF Header and Item Structure

```
Offset  Size  Field
------  ----  -----------
0       2     Item Count (little-endian, typically 2 for UCMM)
2       ...   Address Item
        ...   Data Item
```

#### 4.3.2. Address and Data Items

Each item has the following structure:

```
Offset  Size  Field
------  ----  -----------
0       2     Type ID (little-endian)
2       2     Length (little-endian, does not include Type ID and Length fields)
4       ...   Item Data (length bytes)
```

#### 4.3.3. Item Type IDs

* `0x0000` - Null Address Item
* `0x80B1` - Connected Transport Packet
* `0x00B2` - Unconnected Message (UCMM) - Used for Explicit messaging
* `0x8000` - SOCKADD Info (Originator to Target)
* `0x8001` - SOCKADDR Info (Target to Originator)
* `0x8002` - Sequenced Address Item

#### 4.3.4. Unconnected Message Format (Typical Request)

For `aphyt` client connections, the simulator receives UCMM messages. The CPF typically contains:

1. **Address Item (Null):**

```
Offset  Size  Field
------  ----  -----------
0       2     Type ID: 0x0000
2       2     Length: 0x0000
```

2. **Data Item (Unconnected Message):**

```
Offset  Size  Field
------  ----  -----------
0       2     Type ID: 0x00B2
2       2     Length: (variable)
4       ...   CIP Request (see CIP Layer)
```

**Example CPF Unwrap (Request):**

Raw bytes received at offset 30 in `SendRRData` command:

```
02 00           Item count: 2
00 00           Null address type
00 00           Null address length
00 B2           Unconnected message type
10 00           Length: 16 bytes of CIP data
4C 03           CIP service: Read Tag (0x4C), path length: 3 words
20 6B           Class ID: 0x6B (Variable Object)
24 01           Instance ID: 1
01 00           Number of elements to read: 1
```

#### 4.3.5. CPF Response Wrapping

The simulator constructs a CPF response with:

1. **Address Item (Null):**

```
Offset  Size  Field
------  ----  -----------
0       2     Type ID: 0x0000
2       2     Length: 0x0000
```

2. **Data Item (Unconnected Message):**

```
Offset  Size  Field
------  ----  -----------
0       2     Type ID: 0x00B2
2       2     Length: (CIP reply size)
4       ...   CIP Reply
```

**Processing Logic:**

* **Unwrapping (Request):**
  * Parses the Item Count (should be 2 for UCMM).
  * Iterates through items to find the **Unconnected Message Item (Type ID 0x00B2)**.
  * Extracts the raw CIP Request bytes from this item and passes them to the **CIP Layer**.
* **Wrapping (Response):**
  * Receives the raw CIP Reply bytes from the CIP Layer.
  * Constructs a CPF packet containing a Null Address Item (0x0000) and an Unconnected Message Item (0x00B2) holding the CIP Reply.
  * Returns to EIP layer to construct the full `SendRRData` response.

### 4.4. Layer 3: CIP (Common Industrial Protocol)

The CIP layer acts as the router and object model. It parses the **Request Path** (Class, Instance, Attribute) and executes the requested Service.

#### 4.4.1. CIP Request/Reply Format

**CIP Request Structure:**

```
Offset  Size  Field
------  ----  -----------
0       1     Service Code
1       1     Request Path Size (in words, 1 word = 2 bytes)
2       ...   Request Path (variable length, must be word-aligned)
        ...   Request Data (variable, service-specific)
```

**CIP Reply Structure:**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service Code (original service | 0x80)
1       1     Reserved (0x00)
2       1     General Status (0x00=success, non-zero=error)
3       1     Extended Status Size (in words)
4       ...   Extended Status (size*2 bytes, typically 0 if General Status=0x00)
        ...   Reply Data (variable, service-specific)
```

#### 4.4.2. Request Path Encoding

Request paths use **Logical Segments** to address CIP objects:

**Logical Segment Format:**

```
Offset  Size  Field
------  ----  -----------
0       1     Segment Type & Bit Width
1+      ...   ID Value
```

**Segment Type Codes:**

* `0x20` - 8-bit Class ID
* `0x21` - 16-bit Class ID (followed by 1 padding byte)
* `0x24` - 8-bit Instance ID
* `0x25` - 16-bit Instance ID (followed by 1 padding byte)
* `0x30` - 8-bit Attribute ID
* `0x31` - 16-bit Attribute ID (followed by 1 padding byte)
* `0x28` - 8-bit Element ID
* `0x29` - 16-bit Element ID (followed by 1 padding byte)
* `0x2A` - 32-bit Element ID (followed by 1 padding byte)
* `0x80` - Simple Data Segment (offset/size for partial reads)
* `0x91` - Extended Symbol Segment (ASCII name)

**Example: Get Attribute All on Class 0x6B, Instance 1**

```
Request Path bytes:
  0x20 0x6B        Class ID: 0x6B (Variable Object)
  0x24 0x01        Instance ID: 1

Request Path Size = 4 bytes / 2 = 2 words
```

#### 4.4.3. CIP Service Codes and Payloads

##### Get Attribute All Service (0x01)

**Request:**

```
Offset  Size  Field
------  ----  -----------
0       1     Service: 0x01
1       1     Request Path Size (words)
2       ...   Request Path (Class/Instance, no attribute needed)
```

**Response (Success):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0x81 (0x01 | 0x80)
1       1     Reserved: 0x00
2       1     General Status: 0x00
3       1     Extended Status Size: 0x00
4       ...   Attribute Data (variable, object-specific)
```

##### Read Tag Service (0x4C)

**Request:**

```
Offset  Size  Field
------  ----  -----------
0       1     Service: 0x4C
1       1     Request Path Size (words)
2       ...   Request Path (Class/Instance, or symbolic name)
        2     Number of Elements (little-endian, typically 1)
```

**Response (Success):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0xCC (0x4C | 0x80)
1       1     Reserved: 0x00
2       1     General Status: 0x00 (success)
3       1     Extended Status Size: 0x00
4       1     Data Type Code (see CIP Data Types)
5       1     Reserved: 0x00
6       2     Number of Elements (little-endian)
8       ...   Data (variable length, type-specific)
```

**Response (Error):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0xCC
1       1     Reserved: 0x00
2       1     General Status: (error code, e.g., 0x04=Service not recognized)
3       1     Extended Status Size: (0x00 or 2+ if additional info)
4+      ...   Extended Status Data (optional)
```

##### Write Tag Service (0x4D)

**Request:**

```
Offset  Size  Field
------  ----  -----------
0       1     Service: 0x4D
1       1     Request Path Size (words)
2       ...   Request Path (Class/Instance)
        1     Data Type Code
        1     Reserved: 0x00
        2     Number of Elements (little-endian)
        ...   Data (value to write)
```

**Response:**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0xCD (0x4D | 0x80)
1       1     Reserved: 0x00
2       1     General Status: 0x00 (success)
3       1     Extended Status Size: 0x00
```

##### Get Instance List (0x5F) - Omron Specific

**Request:**

```
Offset  Size  Field
------  ----  -----------
0       1     Service: 0x5F
1       1     Request Path Size (words), typically 2
2       ...   Request Path (Class 0x6A, Instance 0)
        4     Start Instance ID (little-endian)
        4     Number of Instances to Return (little-endian)
        2     Kind: 1=System, 2=User (little-endian)
```

**Response:**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0xDF (0x5F | 0x80)
1       1     Reserved: 0x00
2       1     General Status: 0x00
3       1     Extended Status Size: 0x00
4       2     Number of Instances Returned (little-endian)
6       1     More Items Available (0=no, 1=yes)
7       1     Reserved
8       ...   Instance Records (variable)
```

Each Instance Record:

```
Offset  Size  Field
------  ----  -----------
0       2     Data Length (includes all fields below)
2       2     Class ID (little-endian, e.g., 0x6B)
4       4     Instance ID (little-endian)
8       1     Name Length (in bytes)
9       ...   Name (ASCII, variable length)
        1+    Padding if needed (word-align)
```

#### 4.4.4. The Object Model (Omron Specifics)

To support `aphyt`'s discovery features, the following classes must be implemented:

##### 1. Tag Name Server (Class 0x6A)

* **Role:** The directory of all variables.
* **Service 0x5F (Get Instance List):**
  * Returns a list of variable metadata (Instance ID, Name, Type).
  * Used by `aphyt` to populate its variable dictionary.
* **Service 0x01 (Get Attribute All):**
  * Returns the total count of variables.

**Get Attribute All Response (Class 0x6A, Instance 0):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0x81
1       1     Reserved: 0x00
2       1     General Status: 0x00
3       1     Extended Status Size: 0x00
4       2     Reserved: 0x00
6       2     Total Number of Variables (little-endian)
```

##### 2. Variable Object (Class 0x6B)

* **Role:** Represents a specific variable instance (e.g., `TestInt`).
* **Instance IDs:** Assigned sequentially (1, 2, 3, ...) in order of variable discovery during configuration parsing.
* **Service 0x01 (Get Attribute All):**
  * Returns detailed metadata: Size, CIP Data Type, Array Dimensions, and the **Variable Type Instance ID**.
* **Service 0x4C (Read Tag) & 0x4D (Write Tag):**
  * Handles reading/writing data to the variable's memory buffer.
  * Must support **Simple Data Segments (0x80)** for partial reads/writes of large structures (Offset + Size).

**Get Attribute All Response (Class 0x6B, Instance N):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0x81
1       1     Reserved: 0x00
2       1     General Status: 0x00
3       1     Extended Status Size: 0x00
4       2     Size in Memory (little-endian, in bytes)
6       1     CIP Data Type Code (0xC3=INT, 0xC4=DINT, 0xA0=Structure, etc.)
7       1     CIP Data Type of Array (0x00 if not array)
8       1     Array Dimension Count (0=scalar, 1=1D array, etc.)
9       1     Padding/Reserved
10      4     Number of Elements (dimension 1) (little-endian) [if array]
14      4     Number of Elements (dimension 2) (little-endian) [if multi-dim]
18      4     Number of Elements (dimension 3) (little-endian) [if 3D]
...
16+(dim*4)  1  Bit Number (for BOOL, which bit in the byte)
17+(dim*4)  2  Padding/Reserved
19+(dim*4)  4  Variable Type Instance ID (for structures) (little-endian)
23+(dim*4)  ..  Array Start Elements (if applicable)
```

**Simple Data Segment Request Path Extension:**

When reading/writing only part of a large variable, extend the request path with:

```
Offset  Size  Field
------  ----  -----------
0       1     Simple Data Type Code: 0x80
1       1     Segment Length: 0x03 (fixed, in words)
2       4     Offset in Bytes (little-endian)
6       2     Size in Bytes to Read/Write (little-endian)
```

##### 3. Variable Type Object (Class 0x6C)

* **Role:** Describes the schema of complex structures.
* **Instance IDs:** Assigned sequentially continuing from Class 0x6B instances (e.g., if last variable is Instance 50, then structures start at Instance 51).
* **Service 0x01 (Get Attribute All):**
  * Returns structure metadata: Name, Size, Member Count.
  * **Linked List Definition:** Returns the `Nesting Variable Type Instance ID` (first member) and `Next Instance ID` (next sibling). This allows `aphyt` to recursively reconstruct the structure tree.

**Get Attribute All Response (Class 0x6C, Instance N):**

```
Offset  Size  Field
------  ----  -----------
0       1     Reply Service: 0x81
1       1     Reserved: 0x00
2       1     General Status: 0x00
3       1     Extended Status Size: 0x00
4       4     Size in Memory (little-endian, in bytes)
8       1     CIP Data Type Code: 0xA0 or 0xA2 (structure)
9       1     CIP Data Type of Array: 0x00 (not array)
10      1     Array Dimension Count: 0
11      1     Padding/Reserved
12      2     Number of Members (little-endian)
14      2     Reserved
16      2     CRC Code (little-endian, checksum of structure)
18      1     Variable Type Name Length (in bytes)
19      1+    Variable Type Name (ASCII, variable length)
        ...   Padding to align
        4     Next Instance ID (little-endian, first member)
        4     Nesting Variable Type Instance ID
        4*D   Start Array Elements (if applicable)
```

**Member Linking Example:**

For a structure with multiple members, each member is represented as a separate Class 0x6C instance:

```
Structure "MyPoint" (Instance ID 100):
  - Size: 4 bytes
  - Member Count: 2
  - First Member Instance ID: 101

Member "x" (Instance ID 101):
  - Type: INT (0xC3)
  - Size: 2 bytes
  - Next Instance ID: 102 (points to next member)

Member "y" (Instance ID 102):
  - Type: INT (0xC3)
  - Size: 2 bytes
  - Next Instance ID: 0 (end of chain)
```

#### 4.4.5. CIP Data Type Codes

* `0xC1` - BOOL (1 byte, or bit-packed in arrays)
* `0xC2` - SINT (signed 8-bit integer)
* `0xC3` - INT (signed 16-bit integer, little-endian)
* `0xC4` - DINT (signed 32-bit integer, little-endian)
* `0xCA` - REAL (32-bit float, little-endian)
* `0xA0` - Structure (full definition)
* `0xA2` - Structure (abbreviated/handle)
* `0xD0` - String (variable length, Omron format)

#### 4.4.6. Data Handling

**Serialization Rules:**

* **Scalar Types (Little Endian):**
  * `INT`: 2 bytes
  * `DINT`, `REAL`: 4 bytes
  * `SINT`: 1 byte

* **BOOL Handling:**
  * Single BOOL: 1 byte (0x00 or 0x01)
  * BOOL Arrays: Bit-packed into 16-bit chunks
    * Example: 23 bits requires 4 bytes (2 complete 16-bit chunks)
    * Last byte may contain undefined bits beyond the array size
    * Bit layout is internal; exact order verified against aphyt

* **Structures:**
  * Members are aligned based on their size:
    * 1-byte types (BOOL, SINT): align to 1-byte boundary
    * 2-byte types (INT): align to 2-byte boundary (0x0000, 0x0002, 0x0004, ...)
    * 4-byte types (DINT, REAL): align to 4-byte boundary (0x0000, 0x0004, 0x0008, ...)
  * Structure overall size is padded to the alignment of the largest member
  * Example:
    ```
    struct Point {
      x: BOOL (1 byte)       → offset 0
      y: INT (2 bytes)       → offset 2 (aligned to 2)
      z: DINT (4 bytes)      → offset 4 (aligned to 4)
    } → total size 8 bytes (padded to DINT alignment)
    ```

* **Addressing:**
  * Requests may use **Symbolic Segments** (ASCII names) or **Logical Segments** (Class/Instance IDs).
  * The simulator must resolve Symbolic names to their corresponding Instance IDs.
  * Simple Data Segments enable efficient partial reads/writes for large objects.

* **Response Packet Sizing:**
  * If requested data does not fit in the response packet, the simulator returns an error status
  * Client must use Simple Data Segment (offset/size) to request partial data
  * Only one outstanding request per session (strict request/response protocol)

## 5. aphyt Client Request Flows

This section documents the specific sequences of CIP requests that the `aphyt` client library will send to the simulator. Understanding these flows is critical for proper simulator implementation.

### 5.1. Discovery Flow - Variable Dictionary Population

**Triggered by:** `NSeries.update_variable_dictionary()` or `NSeries.read_variable()` / `NSeries.write_variable()` (on first access)

**Step 1: Get Variable Count**

```
Request:
  Service: 0x01 (Get Attribute All)
  Path: Class 0x6A, Instance 0x00
  
Response:
  Status: 0x00 (success)
  Data (8 bytes):
    Offset 4-5: Reserved (0x0000)
    Offset 6-7: Total variable count (little-endian)
    
Example: Response indicates 5 user variables
```

**Step 2: Get User Variable List**

```
Request (repeated until all variables retrieved):
  Service: 0x5F (Get Instance List)
  Path: Class 0x6A, Instance 0x00
  Data:
    Start Instance ID: 1 (first variable)
    Number of Instances: 100 (request up to 100 at a time)
    Kind: 2 (user-defined variables)
    
Response:
  Status: 0x00 (success)
  Data:
    Offset 4-5: Number returned (e.g., 3)
    Offset 6: More available flag (0=no more, 1=more to fetch)
    Offset 8+: Instance records (each with ID, class, name)
    
Example Instance Record (for variable "TestInt"):
    Data Length: 0x000E (14 bytes)
    Class ID: 0x6B (Variable Object)
    Instance ID: 0x00000001 (little-endian)
    Name Length: 0x07 (7 chars)
    Name: "TestInt" (ASCII)
    Padding: 0x00 (word-align)
```

**Step 3: Discover Type of Each Variable (Cached)**

For each discovered variable, `aphyt` calls `_get_instance_from_variable_name()`:

```
Request:
  Service: 0x01 (Get Attribute All)
  Path: Class 0x6B, Instance 1
  
Response (Simple Scalar Type Example - INT):
  Status: 0x00 (success)
  Data (24+ bytes):
    Offset 4-5: Size: 2 bytes
    Offset 6: Type: 0xC3 (INT)
    Offset 7: Array type: 0x00 (not array)
    Offset 8: Array dimension: 0 (scalar)
    Offset 9: Padding
    Offset 10-13: Reserved
    Offset 14-17: Reserved
    Offset 18-21: Variable Type Instance ID: 0x00000000 (no structure)
    ...
```

**Step 4: If Variable is a Structure - Recursively Discover Members**

For a structure variable, `aphyt` will:
1. Extract the Variable Type Instance ID from the Get Attribute All response
2. Call Get Attribute All on Class 0x6C with that Instance ID
3. Walk the member chain using Next Instance ID pointers

```
Example: Variable "Line1" is of type "MyLine" (a structure)

First, Get Attribute All on Class 0x6B, Instance 2 (Line1 variable):
  Response includes: Variable Type Instance ID = 100
  
Then, Get Attribute All on Class 0x6C, Instance 100 (MyLine structure definition):
  Response (24+ bytes):
    Offset 4-7: Size: 4 bytes
    Offset 8: Type: 0xA0 (Structure)
    Offset 12-13: Member Count: 2
    Offset 16-17: CRC Code: (checksum)
    Offset 18: Name Length: 6
    Offset 19+: Name: "MyLine"
    Offset ~25: Next Instance ID: 101 (first member "start")
    Offset ~29: Nesting Instance ID: (type of first member)
    
For each member, Get Attribute All on Class 0x6C, Instance 101 (first member):
  Response shows member name, type, and Next Instance ID: 102
  
For each member, Get Attribute All on Class 0x6C, Instance 102 (second member):
  Response shows member name, type, and Next Instance ID: 0 (end)
```

### 5.2. Read Flow - Reading a Variable Value

**Triggered by:** `NSeries.read_variable(variable_name)`

**Case 1: Simple Scalar Type (INT, DINT, BOOL, REAL)**

```
Request:
  Service: 0x4C (Read Tag)
  Path: Class 0x6B, Instance 1
  Data: Number of Elements: 1
  
Response:
  Status: 0x00 (success)
  Data:
    Offset 4: Type: 0xC3 (INT)
    Offset 5: Reserved
    Offset 6-7: Number of elements: 1
    Offset 8+: Raw value (2 bytes for INT, little-endian)
    
Example: Reading TestInt = 42
  Response bytes at offset 8+: 0x2A 0x00 (42 in little-endian)
```

**Case 2: Large Structure (Requires Segmented Read)**

For structures larger than the UCMM buffer (~500 bytes), `aphyt` uses Simple Data Segments:

```
Request 1 (first segment):
  Service: 0x4C (Read Tag)
  Path: Class 0x6B, Instance 2 (Line1)
       + Simple Data Segment: Offset=0, Size=494 bytes
  Data: Number of Elements: 1
  
Response 1:
  Status: 0x00 (success)
  Data: (494 bytes of structure data starting at offset 0)

Request 2 (if structure > 494 bytes):
  Service: 0x4C (Read Tag)
  Path: Class 0x6B, Instance 2
       + Simple Data Segment: Offset=494, Size=494 bytes
  Data: Number of Elements: 1
  
Response 2:
  Status: 0x00 (success)
  Data: (remaining structure data)
```

**Case 3: Array of Strings**

```
For each array element, read individually:
  Request (for element [0]):
    Service: 0x4C (Read Tag)
    Path: Class 0x6B, Instance N
         + Element Segment: [0]
    
  Response: String data
  
  Request (for element [1]):
    Service: 0x4C (Read Tag)
    Path: Class 0x6B, Instance N
         + Element Segment: [1]
    
  Response: String data
  ...
```

### 5.3. Write Flow - Writing a Variable Value

**Triggered by:** `NSeries.write_variable(variable_name, value)` or `NSeries.verified_write_variable()`

**Case 1: Simple Scalar Type**

```
Request:
  Service: 0x4D (Write Tag)
  Path: Class 0x6B, Instance 1
  Data:
    Type: 0xC3 (INT)
    Reserved: 0x00
    Number of Elements: 1
    Value: (2 bytes, little-endian)
    
Example: Writing TestInt = 100
  Data: 0xC3 0x00 0x01 0x00 0x64 0x00
         ^Type ^Reserved ^Count (1) ^Value (100)
  
Response:
  Status: 0x00 (success)
  Data: (empty or acknowledgment)
```

**Case 2: Large Structure (Requires Segmented Write)**

```
Request 1 (first segment):
  Service: 0x4D (Write Tag)
  Path: Class 0x6B, Instance 2 (Line1)
       + Simple Data Segment: Offset=0, Size=494 bytes
  Data:
    Type: 0xA2 (Abbreviated Structure)
    Additional Info Length: 2
    CRC Code: (structure checksum, 2 bytes)
    Number of Elements: 1
    Payload: (494 bytes of structure data)
  
Response 1:
  Status: 0x00 (success)

Request 2 (remaining segment):
  Service: 0x4D (Write Tag)
  Path: Class 0x6B, Instance 2
       + Simple Data Segment: Offset=494, Size=(remaining) bytes
  Data:
    Type: 0xA2
    CRC Code: (same as first segment)
    Payload: (remaining bytes)
  
Response 2:
  Status: 0x00 (success)
```

**Case 3: Array of Strings**

```
For each array element, write individually:
  Request (for element [0]):
    Service: 0x4D (Write Tag)
    Path: Class 0x6B, Instance N
         + Element Segment: [0]
    Data:
      Type: 0xD0 (String)
      Length prefix: (2 bytes, string length)
      String data: (ASCII)
    
  Response: 0x00 (success)
  
  Request (for element [1]):
    ...
```

### 5.4. Caching Behavior

**Important:** `aphyt` caches variable metadata to avoid repeated discovery:

- **First access:** `update_variable_dictionary()` or first `read_variable()` call triggers full discovery
- **Subsequent accesses:** Use cached type information to construct read/write requests directly
- **Dictionary persistence:** `save_current_dictionary()` / `load_dictionary_file()` allow pickling the cache to disk

The simulator does **not** need to handle requests any differently based on caching, but knowing this explains why the simulator might see:
1. Heavy burst of discovery requests (Get Attribute All, Get Instance List)
2. Followed by many read/write requests without additional discovery

## 6. Implementation Notes and Validation

### 6.1. Instance ID Assignment Strategy

**Sequential Assignment:**
- Class 0x6A (Tag Name Server): Always Instance 0
- Class 0x6B (Variable Objects): Instance IDs 1, 2, 3, ... assigned in order of variable discovery
- Class 0x6C (Variable Type Objects): Instance IDs continue sequentially after variable objects

**Name-to-ID Mapping:**
- Maintain a lookup table: variable_name → Class 0x6B Instance ID
- Build this during configuration parsing
- Use for resolving symbolic request paths to logical paths

### 6.2. Critical Details to Verify Against aphyt

The following details are inferred from code analysis but should be validated by running aphyt against your simulator:

1. **Exact byte offsets in Class 0x6C Get Attribute All response**
   - Offset of "Next Instance ID" field
   - Offset of "Nesting Variable Type Instance ID" field
   - String name length encoding and padding
   - How to correctly traverse member chain

2. **BOOL Array Bit-Packing Details**
   - Exact bit order within each 16-bit chunk
   - Byte order of 16-bit chunks (little-endian?)
   - Behavior and interpretation of undefined bits in final byte

3. **Structure Member Alignment Edge Cases**
   - Handling of mixed-size structures
   - Nested structure alignment rules
   - Array of structures alignment
   - Padding calculations

4. **Error Responses for Oversized Data**
   - Exact General Status code when data won't fit in response packet
   - Extended Status content (if any)
   - Whether offset beyond variable size returns error or partial data

5. **CIP Service 0x0A (Multiple Services in One Packet)**
   - Format of packed requests
   - How responses are packed in reply
   - Whether essential or optional for basic functionality

### 6.3. Validation Methodology

**Critical: The simulator IS the primary validation mechanism.** Without actual hardware, the only way to validate your implementation is:

1. **Implement the simulator** based on this design document
2. **Run aphyt client against the simulator**
3. **Capture and analyze any failures**
4. **Compare actual vs. expected protocol behavior**
5. **Adjust simulator implementation based on findings**
6. **Iterate until aphyt can perform all operations successfully**

**Testing Checklist:**

```
Discovery Phase:
[ ] aphyt.update_variable_dictionary() succeeds
[ ] All variables discovered with correct names
[ ] Structure types discovered with correct members
[ ] System variables (_prefix) separated from user variables

Read Operations:
[ ] Read simple types: INT, DINT, BOOL, REAL, SINT
[ ] Read structures: simple and nested structures
[ ] Read arrays: scalar arrays, structure arrays
[ ] Read large data: using Simple Data Segment (offset/size)
[ ] Verify structure alignment: padding calculated correctly
[ ] Verify BOOL array packing: bit-packing correct

Write Operations:
[ ] Write simple types
[ ] Write structures with correct alignment
[ ] Write arrays
[ ] Write large data using segments
[ ] Values persist to state.bin
[ ] Subsequent reads return written values

Error Handling:
[ ] Invalid instance IDs return error status
[ ] Oversized read requests return error status
[ ] Requests beyond variable size handled correctly
[ ] Multiple sequential read/write operations work

Stress Tests:
[ ] 100+ variables
[ ] Large structures (>500 bytes)
[ ] Deeply nested structures (5+ levels)
[ ] Mixed scalar and structure arrays
[ ] Rapid read/write cycles
```

### 6.4. Debugging with Raw Packet Capture

When debugging mismatches, capture the raw EIP/CPF/CIP bytes:

```
Pseudo-code for request capture:
  Print EIP Header: 24 bytes
  Print CPF Item Count + Items
  Print CIP Service + Path + Data
  
Compare against protocol specifications to identify mismatches
```

Store captured packets for later analysis and comparison against expected protocol format.

## 7. Development Plan

1. **Scaffold Server:** Implement the `selectors` based TCP server loop.
2. **EIP & CPF Stack:** Implement packet parsing for EIP headers and CPF item lists.
3. **CIP Router:** Create the dispatch logic for CIP Services.
4. **Discovery Implementation:**
   * Implement Class 0x6A (Tag Name Server).
   * Implement Class 0x6B (Variable Object).
   * Implement Class 0x6C (Variable Type Object).
   * *Goal:* `aphyt` should successfully run `update_variable_dictionary()`.
5. **Read/Write Implementation:**
   * Implement Read/Write services for basic types.
   * Implement segmented access for structures.
6. **Integration Test:** Verify with the existing `aphyt` client.
