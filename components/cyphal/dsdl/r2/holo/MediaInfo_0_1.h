// This is an AUTO-GENERATED Cyphal DSDL data type implementation. Curious? See https://opencyphal.org.
// You shouldn't attempt to edit this file.
//
// Checking this file under version control is not recommended unless it is used as part of a high-SIL
// safety-critical codebase. The typical usage scenario is to generate it as part of the build process.
//
// To avoid conflicts with definitions given in the source DSDL file, all entities created by the code generator
// are named with an underscore at the end, like foo_bar_().
//
// Generator:     nunavut-2.3.1 (serialization was enabled)
// Source file:   external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl
// Is deprecated: no
// Fixed port-ID: None
// Full name:     r2.holo.MediaInfo
// Version:       0.1
//
// Language Options
//     target_endianness:  any
//     omit_float_serialization_support:  False
//     enable_serialization_asserts:  False
//     enable_override_variable_array_capacity:  False
//     cast_format:  (({type}) {value})
//     std:  c11

#ifndef R2_HOLO_MEDIA_INFO_0_1_INCLUDED_
#define R2_HOLO_MEDIA_INFO_0_1_INCLUDED_

#include <nunavut/support/serialization.h>
#include <r2/holo/Error_0_1.h>
#include <stdint.h>
#include <stdlib.h>
#include <uavcan/file/Path_2_0.h>

static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_TARGET_ENDIANNESS == 1693710260,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_OMIT_FLOAT_SERIALIZATION_SUPPORT == 0,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_SERIALIZATION_ASSERTS == 0,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_OVERRIDE_VARIABLE_ARRAY_CAPACITY == 0,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_CAST_FORMAT == 2368206204,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_STD == 575908835,
              "external/r2_cyphal_dsdl/r2/holo/MediaInfo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );

#ifdef __cplusplus
extern "C" {
#endif

/// This type does not have a fixed port-ID. See https://forum.opencyphal.org/t/choosing-message-and-service-ids/889
#define r2_holo_MediaInfo_0_1_HAS_FIXED_PORT_ID_ false

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.MediaInfo.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_MediaInfo_0_1_FULL_NAME_             "r2.holo.MediaInfo"
#define r2_holo_MediaInfo_0_1_FULL_NAME_AND_VERSION_ "r2.holo.MediaInfo.0.1"

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.MediaInfo.Request.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_MediaInfo_Request_0_1_FULL_NAME_             "r2.holo.MediaInfo.Request"
#define r2_holo_MediaInfo_Request_0_1_FULL_NAME_AND_VERSION_ "r2.holo.MediaInfo.Request.0.1"

/// Extent is the minimum amount of memory required to hold any serialized representation of any compatible
/// version of the data type; or, on other words, it is the the maximum possible size of received objects of this type.
/// The size is specified in bytes (rather than bits) because by definition, extent is an integer number of bytes long.
/// When allocating a deserialization (RX) buffer for this data type, it should be at least extent bytes large.
/// When allocating a serialization (TX) buffer, it is safe to use the size of the largest serialized representation
/// instead of the extent because it provides a tighter bound of the object size; it is safe because the concrete type
/// is always known during serialization (unlike deserialization). If not sure, use extent everywhere.
#define r2_holo_MediaInfo_Request_0_1_EXTENT_BYTES_                    300UL
#define r2_holo_MediaInfo_Request_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ 256UL
static_assert(r2_holo_MediaInfo_Request_0_1_EXTENT_BYTES_ >= r2_holo_MediaInfo_Request_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
              "Internal constraint violation");

typedef struct
{
    /// uavcan.file.Path.2.0 path
    uavcan_file_Path_2_0 path;
} r2_holo_MediaInfo_Request_0_1;

/// Serialize an instance into the provided buffer.
/// The lifetime of the resulting serialized representation is independent of the original instance.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original object where possible.
///
/// @param obj      The object to serialize.
///
/// @param buffer   The destination buffer. There are no alignment requirements.
///                 @see r2_holo_MediaInfo_Request_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the buffer in bytes.
///                                 Upon return this value will be updated with the size of the constructed serialized
///                                 representation (in bytes); this value is then to be passed over to the transport
///                                 layer. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_MediaInfo_Request_0_1_serialize_(
    const r2_holo_MediaInfo_Request_0_1* const obj, uint8_t* const buffer,  size_t* const inout_buffer_size_bytes)
{
    if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    if ((8U * (size_t) capacity_bytes) < 2048UL)
    {
        return -NUNAVUT_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    // Notice that fields that are not an integer number of bytes long may overrun the space allocated for them
    // in the serialization buffer up to the next byte boundary. This is by design and is guaranteed to be safe.
    size_t offset_bits = 0U;
    {   // uavcan.file.Path.2.0 path
        size_t _size_bytes0_ = 256UL;  // Nested object (max) size, in bytes.
        int8_t _err0_ = uavcan_file_Path_2_0_serialize_(
            &obj->path, &buffer[offset_bits / 8U], &_size_bytes0_);
        if (_err0_ < 0)
        {
            return _err0_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes0_ * 8U;  // Advance by the size of the nested object.
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad0_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err1_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad0_);  // Optimize?
        if (_err1_ < 0)
        {
            return _err1_;
        }
        offset_bits += _pad0_;
    }
    // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
    *inout_buffer_size_bytes = (size_t) (offset_bits / 8U);
    return NUNAVUT_SUCCESS;
}

/// Deserialize an instance from the provided buffer.
/// The lifetime of the resulting object is independent of the original buffer.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original buffer where possible.
///
/// @param obj      The object to update from the provided serialized representation.
///
/// @param buffer   The source buffer containing the serialized representation. There are no alignment requirements.
///                 If the buffer is shorter or longer than expected, it will be implicitly zero-extended or truncated,
///                 respectively; see Specification for "implicit zero extension" and "implicit truncation" rules.
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the supplied serialized
///                                 representation, in bytes. Upon return this value will be updated with the
///                                 size of the consumed fragment of the serialized representation (in bytes),
///                                 which may be smaller due to the implicit truncation rule, but it is guaranteed
///                                 to never exceed the original buffer size even if the implicit zero extension rule
///                                 was activated. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_MediaInfo_Request_0_1_deserialize_(
    r2_holo_MediaInfo_Request_0_1* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)
{
    if ((out_obj == NULL) || (inout_buffer_size_bytes == NULL) || ((buffer == NULL) && (0 != *inout_buffer_size_bytes)))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    if (buffer == NULL)
    {
        buffer = (const uint8_t*)"";
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    const size_t capacity_bits = capacity_bytes * (size_t) 8U;
    size_t offset_bits = 0U;
    // uavcan.file.Path.2.0 path
    {
        size_t _size_bytes1_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err2_ = uavcan_file_Path_2_0_deserialize_(
            &out_obj->path, &buffer[offset_bits / 8U], &_size_bytes1_);
        if (_err2_ < 0)
        {
            return _err2_;
        }
        offset_bits += _size_bytes1_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    *inout_buffer_size_bytes = (size_t) (nunavutChooseMin(offset_bits, capacity_bits) / 8U);
    return NUNAVUT_SUCCESS;
}

/// Initialize an instance to default values. Does nothing if @param out_obj is NULL.
/// This function intentionally leaves inactive elements uninitialized; for example, members of a variable-length
/// array beyond its length are left uninitialized; aliased union memory that is not used by the first union field
/// is left uninitialized, etc. If full zero-initialization is desired, just use memset(&obj, 0, sizeof(obj)).
static inline void r2_holo_MediaInfo_Request_0_1_initialize_(r2_holo_MediaInfo_Request_0_1* const out_obj)
{
    if (out_obj != NULL)
    {
        size_t size_bytes = 0;
        const uint8_t buf = 0;
        const int8_t err = r2_holo_MediaInfo_Request_0_1_deserialize_(out_obj, &buf, &size_bytes);

        (void) err;
    }
}

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.MediaInfo.Response.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_MediaInfo_Response_0_1_FULL_NAME_             "r2.holo.MediaInfo.Response"
#define r2_holo_MediaInfo_Response_0_1_FULL_NAME_AND_VERSION_ "r2.holo.MediaInfo.Response.0.1"

/// Extent is the minimum amount of memory required to hold any serialized representation of any compatible
/// version of the data type; or, on other words, it is the the maximum possible size of received objects of this type.
/// The size is specified in bytes (rather than bits) because by definition, extent is an integer number of bytes long.
/// When allocating a deserialization (RX) buffer for this data type, it should be at least extent bytes large.
/// When allocating a serialization (TX) buffer, it is safe to use the size of the largest serialized representation
/// instead of the extent because it provides a tighter bound of the object size; it is safe because the concrete type
/// is always known during serialization (unlike deserialization). If not sure, use extent everywhere.
#define r2_holo_MediaInfo_Response_0_1_EXTENT_BYTES_                    128UL
#define r2_holo_MediaInfo_Response_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ 81UL
static_assert(r2_holo_MediaInfo_Response_0_1_EXTENT_BYTES_ >= r2_holo_MediaInfo_Response_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
              "Internal constraint violation");

/// saturated uint8 NONE = 0
#define r2_holo_MediaInfo_Response_0_1_NONE (0U)

/// saturated uint8 CLIP = 1
#define r2_holo_MediaInfo_Response_0_1_CLIP (1U)

/// saturated uint8 IMAGE = 2
#define r2_holo_MediaInfo_Response_0_1_IMAGE (2U)

/// saturated uint8 ANIMATION = 3
#define r2_holo_MediaInfo_Response_0_1_ANIMATION (3U)

/// saturated uint8 FORMAT_NONE = 0
#define r2_holo_MediaInfo_Response_0_1_FORMAT_NONE (0U)

/// saturated uint8 FORMAT_MOV = 1
#define r2_holo_MediaInfo_Response_0_1_FORMAT_MOV (1U)

/// saturated uint8 FORMAT_PNG = 2
#define r2_holo_MediaInfo_Response_0_1_FORMAT_PNG (2U)

/// saturated uint8 FORMAT_JPEG = 3
#define r2_holo_MediaInfo_Response_0_1_FORMAT_JPEG (3U)

/// saturated uint8 FORMAT_GIF = 4
#define r2_holo_MediaInfo_Response_0_1_FORMAT_GIF (4U)

typedef struct
{
    /// r2.holo.Error.0.1 result
    r2_holo_Error_0_1 result;

    /// saturated uint8 kind
    uint8_t kind;

    /// saturated uint8 format
    uint8_t format;

    /// saturated uint16 width
    uint16_t width;

    /// saturated uint16 height
    uint16_t height;

    /// saturated uint16 frames
    uint16_t frames;

    /// saturated float16 fps
    float fps;

    /// saturated uint32 duration_ms
    uint32_t duration_ms;
} r2_holo_MediaInfo_Response_0_1;

/// Serialize an instance into the provided buffer.
/// The lifetime of the resulting serialized representation is independent of the original instance.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original object where possible.
///
/// @param obj      The object to serialize.
///
/// @param buffer   The destination buffer. There are no alignment requirements.
///                 @see r2_holo_MediaInfo_Response_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the buffer in bytes.
///                                 Upon return this value will be updated with the size of the constructed serialized
///                                 representation (in bytes); this value is then to be passed over to the transport
///                                 layer. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_MediaInfo_Response_0_1_serialize_(
    const r2_holo_MediaInfo_Response_0_1* const obj, uint8_t* const buffer,  size_t* const inout_buffer_size_bytes)
{
    if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    if ((8U * (size_t) capacity_bytes) < 648UL)
    {
        return -NUNAVUT_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    // Notice that fields that are not an integer number of bytes long may overrun the space allocated for them
    // in the serialization buffer up to the next byte boundary. This is by design and is guaranteed to be safe.
    size_t offset_bits = 0U;
    {   // r2.holo.Error.0.1 result
        size_t _size_bytes2_ = 67UL;  // Nested object (max) size, in bytes.
        int8_t _err3_ = r2_holo_Error_0_1_serialize_(
            &obj->result, &buffer[offset_bits / 8U], &_size_bytes2_);
        if (_err3_ < 0)
        {
            return _err3_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes2_ * 8U;  // Advance by the size of the nested object.
    }
    {   // saturated uint8 kind
        // Saturation code not emitted -- native representation matches the serialized representation.
        buffer[offset_bits / 8U] = (uint8_t)(obj->kind);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 8U;
    }
    {   // saturated uint8 format
        // Saturation code not emitted -- native representation matches the serialized representation.
        buffer[offset_bits / 8U] = (uint8_t)(obj->format);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 8U;
    }
    {   // saturated uint16 width
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err4_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->width, 16U);
        if (_err4_ < 0)
        {
            return _err4_;
        }
        offset_bits += 16U;
    }
    {   // saturated uint16 height
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err5_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->height, 16U);
        if (_err5_ < 0)
        {
            return _err5_;
        }
        offset_bits += 16U;
    }
    {   // saturated uint16 frames
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err6_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->frames, 16U);
        if (_err6_ < 0)
        {
            return _err6_;
        }
        offset_bits += 16U;
    }
    {   // saturated float16 fps
        float _sat0_ = obj->fps;
        if (isfinite(_sat0_))
        {
            if (_sat0_ < ((float) -65504.0))
            {
                _sat0_ = ((float) -65504.0);
            }
            if (_sat0_ > ((float) 65504.0))
            {
                _sat0_ = ((float) 65504.0);
            }
        }
        const int8_t _err7_ = nunavutSetF16(&buffer[0], capacity_bytes, offset_bits, _sat0_);
        if (_err7_ < 0)
        {
            return _err7_;
        }
        offset_bits += 16U;
    }
    {   // saturated uint32 duration_ms
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err8_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->duration_ms, 32U);
        if (_err8_ < 0)
        {
            return _err8_;
        }
        offset_bits += 32U;
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad1_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err9_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad1_);  // Optimize?
        if (_err9_ < 0)
        {
            return _err9_;
        }
        offset_bits += _pad1_;
    }
    // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
    *inout_buffer_size_bytes = (size_t) (offset_bits / 8U);
    return NUNAVUT_SUCCESS;
}

/// Deserialize an instance from the provided buffer.
/// The lifetime of the resulting object is independent of the original buffer.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original buffer where possible.
///
/// @param obj      The object to update from the provided serialized representation.
///
/// @param buffer   The source buffer containing the serialized representation. There are no alignment requirements.
///                 If the buffer is shorter or longer than expected, it will be implicitly zero-extended or truncated,
///                 respectively; see Specification for "implicit zero extension" and "implicit truncation" rules.
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the supplied serialized
///                                 representation, in bytes. Upon return this value will be updated with the
///                                 size of the consumed fragment of the serialized representation (in bytes),
///                                 which may be smaller due to the implicit truncation rule, but it is guaranteed
///                                 to never exceed the original buffer size even if the implicit zero extension rule
///                                 was activated. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_MediaInfo_Response_0_1_deserialize_(
    r2_holo_MediaInfo_Response_0_1* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)
{
    if ((out_obj == NULL) || (inout_buffer_size_bytes == NULL) || ((buffer == NULL) && (0 != *inout_buffer_size_bytes)))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    if (buffer == NULL)
    {
        buffer = (const uint8_t*)"";
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    const size_t capacity_bits = capacity_bytes * (size_t) 8U;
    size_t offset_bits = 0U;
    // r2.holo.Error.0.1 result
    {
        size_t _size_bytes3_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err10_ = r2_holo_Error_0_1_deserialize_(
            &out_obj->result, &buffer[offset_bits / 8U], &_size_bytes3_);
        if (_err10_ < 0)
        {
            return _err10_;
        }
        offset_bits += _size_bytes3_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    // saturated uint8 kind
    if ((offset_bits + 8U) <= capacity_bits)
    {
        out_obj->kind = buffer[offset_bits / 8U] & 255U;
    }
    else
    {
        out_obj->kind = 0U;
    }
    offset_bits += 8U;
    // saturated uint8 format
    if ((offset_bits + 8U) <= capacity_bits)
    {
        out_obj->format = buffer[offset_bits / 8U] & 255U;
    }
    else
    {
        out_obj->format = 0U;
    }
    offset_bits += 8U;
    // saturated uint16 width
    out_obj->width = nunavutGetU16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated uint16 height
    out_obj->height = nunavutGetU16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated uint16 frames
    out_obj->frames = nunavutGetU16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated float16 fps
    out_obj->fps = nunavutGetF16(&buffer[0], capacity_bytes, offset_bits);
    offset_bits += 16U;
    // saturated uint32 duration_ms
    out_obj->duration_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 32);
    offset_bits += 32U;
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    *inout_buffer_size_bytes = (size_t) (nunavutChooseMin(offset_bits, capacity_bits) / 8U);
    return NUNAVUT_SUCCESS;
}

/// Initialize an instance to default values. Does nothing if @param out_obj is NULL.
/// This function intentionally leaves inactive elements uninitialized; for example, members of a variable-length
/// array beyond its length are left uninitialized; aliased union memory that is not used by the first union field
/// is left uninitialized, etc. If full zero-initialization is desired, just use memset(&obj, 0, sizeof(obj)).
static inline void r2_holo_MediaInfo_Response_0_1_initialize_(r2_holo_MediaInfo_Response_0_1* const out_obj)
{
    if (out_obj != NULL)
    {
        size_t size_bytes = 0;
        const uint8_t buf = 0;
        const int8_t err = r2_holo_MediaInfo_Response_0_1_deserialize_(out_obj, &buf, &size_bytes);

        (void) err;
    }
}

#ifdef __cplusplus
}
#endif
#endif // R2_HOLO_MEDIA_INFO_0_1_INCLUDED_
