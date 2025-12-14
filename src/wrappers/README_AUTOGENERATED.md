# libplctag Language Wrappers - Auto-Generated with SWIG

This directory contains auto-generated language bindings for libplctag created 
using SWIG (Simplified Wrapper and Interface Generator).
See: http://www.swig.org/

## Overview

All wrappers are automatically generated from a single source interface file 
(libplctag.i) that describes the libplctag C API. This ensures:

- Consistency across all language bindings
- Maintainability - changes to the C API are reflected in all languages
- Completeness - all C functions and enums are exposed
- Easy updates - regenerate when libplctag.h changes

## Generated Wrappers

### Java (java/generated/)

Generated Java bindings using JNI (Java Native Interface).

Files:
- libplctag.java - Main wrapper class with static methods
- libplctagJNI.java - JNI bridge class (auto-generated)
- plctag_*.java - Enum classes for error codes, debug levels, etc.
- libplctag_wrap.cxx - Native C++ wrapper code (must be compiled into shared library)

Usage:
```java
import com.libplctag.*;

// Create a tag
int tagId = libplctag.create(
    "protocol=eip&gateway=192.168.1.100&path=1,0&cpu=controllogix&" +
    "name=test_tag&elem_size=4&elem_count=1", 5000);

// Check status
int status = libplctag.status(tagId);
if (status == plctag_error_code_t.PLCTAG_STATUS_OK) {
    System.out.println("Tag created successfully");
}

// Read data
libplctag.read(tagId, 5000);
int value = libplctag.getInt32(tagId, 0);

// Clean up
libplctag.destroy(tagId);
```

Compilation:
```bash
cd java
# Compile Java files
javac *.java

# Compile the C++ wrapper into a shared library
g++ -fPIC -shared -I/path/to/java/include \
    -o liblibplctag.so libplctag_wrap.cxx \
    -L/path/to/libplctag/lib -lplctag
```

### C# (csharp/generated/)

Generated C# bindings using P/Invoke (for Windows/.NET).

Files:
- libplctag.cs - Main wrapper class with static methods
- libplctagPINVOKE.cs - P/Invoke interop class (auto-generated)
- plctag_*.cs - Enum classes for error codes, debug levels, etc.
- libplctag_wrap.cxx - Optional native wrapper (not needed for P/Invoke)

Usage:
```csharp
using com.libplctag;

// Create a tag
int tagId = libplctag.create(
    "protocol=eip&gateway=192.168.1.100&path=1,0&cpu=controllogix&" +
    "name=test_tag&elem_size=4&elem_count=1", 5000);

// Check status
int status = libplctag.status(tagId);
if (status == (int)plctag_error_code_t.PLCTAG_STATUS_OK) {
    Console.WriteLine("Tag created successfully");
}

// Read data
libplctag.read(tagId, 5000);
int value = libplctag.getInt32(tagId, 0);

// Clean up
libplctag.destroy(tagId);
```

Compilation:
```bash
cd csharp
# Compile C# files
csc /target:library /out:libplctag.dll *.cs

# On Windows, ensure libplctag.dll is in the same directory or in PATH
```

### Python (python/generated/)

Generated Python bindings using ctypes/CFFI.

Files:
- libplctag.py - Main wrapper module

Usage:
```python
from libplctag import libplctag

# Create a tag
tag_id = libplctag.create(
    "protocol=eip&gateway=192.168.1.100&path=1,0&cpu=controllogix&" +
    "name=test_tag&elem_size=4&elem_count=1", 5000)

# Check status
status = libplctag.status(tag_id)
if status == libplctag.PLCTAG_STATUS_OK:
    print("Tag created successfully")

# Read data
libplctag.read(tag_id, 5000)
value = libplctag.getInt32(tag_id, 0)

# Clean up
libplctag.destroy(tag_id)
```

## Regenerating Wrappers

When src/libplctag/lib/libplctag.h is updated, regenerate all wrappers:

```bash
cd src/wrappers

# Generate Java
swig -java -package com.libplctag -outdir generated/java \
    -o generated/java/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i

# Generate C#
swig -csharp -dllimport libplctag -outdir generated/csharp \
    -o generated/csharp/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i

# Generate Python
swig -python -outdir generated/python \
    -o generated/python/libplctag_wrap.cxx \
    -I../libplctag/lib libplctag.i
```

Or use CMake:
```bash
cd build
cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF ..
cmake --build . --target swig_java swig_csharp swig_python
```

## SWIG Interface File

The master interface file is libplctag.i. It includes:

- Direct inclusion of libplctag.h for all type and function definitions
- Function name remapping for more idiomatic language conventions
  (e.g., plc_tag_create -> create())
- Enum declarations
- Type mappings for byte arrays

To modify the wrapper interface:
1. Edit libplctag.i
2. Regenerate the wrappers using the commands above

## Function Naming Conventions

SWIG automatically converts C function names to language-specific conventions:

C Name                   | Java/C#/Python
------------------------|-----------
plc_tag_create          | create()
plc_tag_read            | read()
plc_tag_write           | write()
plc_tag_status          | status()
plc_tag_destroy         | destroy()
plc_tag_get_int32       | getInt32()
plc_tag_set_int32       | setInt32()
plc_tag_get_string      | getString()
plc_tag_set_string      | setString()

## Enums

All enums are automatically converted to language-specific enum types:

- plctag_error_code_t - Error codes and status values
- plctag_debug_level_t - Debug output levels
- plctag_debug_module_t - Debug module selectors
- plctag_event_t - Callback event types

## Limitations and Notes

1. Callbacks: The generated bindings provide basic callback support but may 
   require language-specific wrapper code for full functionality.

2. String Handling: String parameters are passed through as-is. Language-specific 
   string handling is available in the wrapper classes.

3. Memory Management: SWIG handles memory management for most types, but be aware 
   that the underlying C library manages the lifetime of tag objects.

4. Byte Arrays: Use appropriate language methods for getting/setting raw bytes.

## Dependencies

- SWIG (version 4.0+): Install via:
  - macOS: brew install swig
  - Linux: apt install swig or yum install swig
  - Windows: Download from http://www.swig.org or use vcpkg

- libplctag development headers and library

- Language-specific tools:
  - Java: JDK with javac and JNI headers
  - C#: .NET SDK or Mono
  - Python: Python development headers

## License

These auto-generated wrappers are provided under the same licenses as libplctag 
(MPL 2.0 or LGPL 2+).

## See Also

- libplctag GitHub: https://github.com/libplctag/libplctag
- SWIG Documentation: http://www.swig.org/Doc.html
- libplctag API Documentation: ../../../README.md
