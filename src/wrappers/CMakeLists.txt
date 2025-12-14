# CMakeLists.txt for SWIG wrapper generation
# 
# This file configures SWIG to automatically generate language bindings
# for Java, C#, Python, and other languages from the libplctag.h header file.
#
# SWIG must be installed:
#   macOS:  brew install swig
#   Linux:  apt install swig (Ubuntu/Debian), yum install swig (RedHat/CentOS)
#   Windows: Download from http://www.swig.org or vcpkg install swig

cmake_minimum_required(VERSION 3.15)

# Find SWIG - required for wrapper generation
find_package(SWIG 4.0 REQUIRED)
if(NOT ${SWIG_FOUND})
    message("Skipping SWIG wrapper generation because SWIG was not found.")
    return()
else()
    message(STATUS "SWIG found: ${SWIG_EXECUTABLE} (version ${SWIG_VERSION})")
endif()

include(${SWIG_USE_FILE})


# Set the SWIG source and output directories
set(SWIG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SWIG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Ensure output directory exists
file(MAKE_DIRECTORY "${SWIG_OUTPUT_DIR}")

# Get path to libplctag header
set(LIBPLCTAG_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libplctag/lib")

# ============================================================================
# Java Wrapper Generation
# ============================================================================

message(STATUS "SWIG: Generating Java bindings...")

set(JAVA_OUTPUT_DIR "${SWIG_OUTPUT_DIR}/java")
file(MAKE_DIRECTORY "${JAVA_OUTPUT_DIR}")

# Run SWIG for Java
add_custom_command(
    OUTPUT "${JAVA_OUTPUT_DIR}/libplctag_wrap.cxx" "${JAVA_OUTPUT_DIR}/libplctag.java"
    COMMAND ${SWIG_EXECUTABLE}
        -java
        -package org.libplctag
        -outdir "${JAVA_OUTPUT_DIR}"
        -o "${JAVA_OUTPUT_DIR}/libplctag_wrap.cxx"
        -I"${LIBPLCTAG_HEADER_DIR}"
        "${SWIG_SOURCE_DIR}/libplctag.i"
    DEPENDS "${SWIG_SOURCE_DIR}/libplctag.i" "${LIBPLCTAG_HEADER_DIR}/libplctag.h"
    COMMENT "Generating Java bindings with SWIG..."
)

# Add custom target for Java generation
add_custom_target(swig_java ALL DEPENDS "${JAVA_OUTPUT_DIR}/libplctag_wrap.cxx" )

# ============================================================================
# C# Wrapper Generation
# ============================================================================

message(STATUS "SWIG: Generating C# bindings...")

set(CSHARP_OUTPUT_DIR "${SWIG_OUTPUT_DIR}/csharp")
file(MAKE_DIRECTORY "${CSHARP_OUTPUT_DIR}")

# Run SWIG for C#
add_custom_command(
    OUTPUT "${CSHARP_OUTPUT_DIR}/libplctag_wrap.cxx" "${CSHARP_OUTPUT_DIR}/libplctag.cs"
    COMMAND ${SWIG_EXECUTABLE}
        -csharp
        -dllimport libplctag
        -outdir "${CSHARP_OUTPUT_DIR}"
        -o "${CSHARP_OUTPUT_DIR}/libplctag_wrap.cxx"
        -I"${LIBPLCTAG_HEADER_DIR}"
        "${SWIG_SOURCE_DIR}/libplctag.i"
    DEPENDS "${SWIG_SOURCE_DIR}/libplctag.i" "${LIBPLCTAG_HEADER_DIR}/libplctag.h"
    COMMENT "Generating C# bindings with SWIG..."
)

# Add custom target for C# generation
add_custom_target(swig_csharp ALL
    DEPENDS "${CSHARP_OUTPUT_DIR}/libplctag_wrap.cxx"
)

# ============================================================================
# Python Wrapper Generation
# ============================================================================

message(STATUS "SWIG: Generating Python bindings...")

set(PYTHON_OUTPUT_DIR "${SWIG_OUTPUT_DIR}/python")
file(MAKE_DIRECTORY "${PYTHON_OUTPUT_DIR}")

# Run SWIG for Python
add_custom_command(
    OUTPUT "${PYTHON_OUTPUT_DIR}/libplctag_wrap.cxx" "${PYTHON_OUTPUT_DIR}/libplctag.py"
    COMMAND ${SWIG_EXECUTABLE}
        -python
        -py3
        -outdir "${PYTHON_OUTPUT_DIR}"
        -o "${PYTHON_OUTPUT_DIR}/libplctag_wrap.cxx"
        -I"${LIBPLCTAG_HEADER_DIR}"
        "${SWIG_SOURCE_DIR}/libplctag.i"
    DEPENDS "${SWIG_SOURCE_DIR}/libplctag.i" "${LIBPLCTAG_HEADER_DIR}/libplctag.h"
    COMMENT "Generating Python bindings with SWIG..."
)

# Add custom target for Python generation
add_custom_target(swig_python ALL DEPENDS "${PYTHON_OUTPUT_DIR}/libplctag_wrap.cxx")

# ============================================================================
# Installation targets
# ============================================================================

# Install Java bindings
install(
    DIRECTORY "${JAVA_OUTPUT_DIR}/"
    DESTINATION "wrappers/java/generated"
    FILES_MATCHING PATTERN "*.java"
)

# Install C# bindings
install(
    DIRECTORY "${CSHARP_OUTPUT_DIR}/"
    DESTINATION "wrappers/csharp/generated"
    FILES_MATCHING PATTERN "*.cs"
)

# Install Python bindings
install(
    DIRECTORY "${PYTHON_OUTPUT_DIR}/"
    DESTINATION "wrappers/python/generated"
    FILES_MATCHING PATTERN "*.py"
)

# ============================================================================
# Convenience target to copy generated files to source tree
# ============================================================================

add_custom_target(copy_java_wrappers
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JAVA_OUTPUT_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/java/generated"
    DEPENDS swig_java
    COMMENT "Copying generated Java bindings to source tree..."
)

add_custom_target(copy_csharp_wrappers
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CSHARP_OUTPUT_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/csharp/generated"
    DEPENDS swig_csharp
    COMMENT "Copying generated C# bindings to source tree..."
)

add_custom_target(copy_python_wrappers
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PYTHON_OUTPUT_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/python/generated"
    DEPENDS swig_python
    COMMENT "Copying generated Python bindings to source tree..."
)

message(STATUS "SWIG wrapper generation configured successfully!")
message(STATUS "Generated files will be in: ${SWIG_OUTPUT_DIR}")
