/*
 * SWIG Interface file for libplctag
 * 
 * This file defines the interface between the C library and language-specific wrappers.
 * SWIG generates bindings for Java, C#, C++, and other languages from this single source.
 * 
 * Build with:
 *   swig -java -package com.libplctag libplctag.i
 *   swig -csharp libplctag.i
 *   swig -c++ libplctag.i
 */

%module libplctag

%{
#include "libplctag.h"
%}

/* Include the C header file to expose all declarations */
%include "libplctag.h"

%feature("autodoc", "1");

/* Handle byte arrays for get_raw_bytes and set_raw_bytes */
%apply uint8_t *INOUT { uint8_t *buffer };
%apply uint8_t *OUTPUT { uint8_t *buffer };

/* Rename functions to more idiomatic language names */
%rename(decodeError) plc_tag_decode_error;
%rename(setDebugLevel) plc_tag_set_debug_level;
%rename(getDebugLevel) plc_tag_get_debug_level;
%rename(create) plc_tag_create;
%rename(createEx) plc_tag_create_ex;
%rename(shutdown) plc_tag_shutdown;
%rename(registerCallback) plc_tag_register_callback;
%rename(registerCallbackEx) plc_tag_register_callback_ex;
%rename(unregisterCallback) plc_tag_unregister_callback;
%rename(registerLogger) plc_tag_register_logger;
%rename(unregisterLogger) plc_tag_unregister_logger;
%rename(lock) plc_tag_lock;
%rename(unlock) plc_tag_unlock;
%rename(abort) plc_tag_abort;
%rename(destroy) plc_tag_destroy;
%rename(read) plc_tag_read;
%rename(status) plc_tag_status;
%rename(write) plc_tag_write;
%rename(getIntAttribute) plc_tag_get_int_attribute;
%rename(setIntAttribute) plc_tag_set_int_attribute;
%rename(getByteArrayAttribute) plc_tag_get_byte_array_attribute;
%rename(getSize) plc_tag_get_size;
%rename(setSize) plc_tag_set_size;
%rename(getBit) plc_tag_get_bit;
%rename(setBit) plc_tag_set_bit;
%rename(getUint64) plc_tag_get_uint64;
%rename(setUint64) plc_tag_set_uint64;
%rename(getInt64) plc_tag_get_int64;
%rename(setInt64) plc_tag_set_int64;
%rename(getUint32) plc_tag_get_uint32;
%rename(setUint32) plc_tag_set_uint32;
%rename(getInt32) plc_tag_get_int32;
%rename(setInt32) plc_tag_set_int32;
%rename(getUint16) plc_tag_get_uint16;
%rename(setUint16) plc_tag_set_uint16;
%rename(getInt16) plc_tag_get_int16;
%rename(setInt16) plc_tag_set_int16;
%rename(getUint8) plc_tag_get_uint8;
%rename(setUint8) plc_tag_set_uint8;
%rename(getInt8) plc_tag_get_int8;
%rename(setInt8) plc_tag_set_int8;
%rename(getFloat64) plc_tag_get_float64;
%rename(setFloat64) plc_tag_set_float64;
%rename(getFloat32) plc_tag_get_float32;
%rename(setFloat32) plc_tag_set_float32;
%rename(setRawBytes) plc_tag_set_raw_bytes;
%rename(getRawBytes) plc_tag_get_raw_bytes;
%rename(getString) plc_tag_get_string;
%rename(setString) plc_tag_set_string;
%rename(getStringLength) plc_tag_get_string_length;
%rename(getStringCapacity) plc_tag_get_string_capacity;
%rename(getStringTotalLength) plc_tag_get_string_total_length;
