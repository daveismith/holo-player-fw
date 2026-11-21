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
// Source file:   external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl
// Is deprecated: no
// Fixed port-ID: None
// Full name:     r2.holo.MotionSpec
// Version:       0.1
//
// Language Options
//     target_endianness:  any
//     omit_float_serialization_support:  False
//     enable_serialization_asserts:  False
//     enable_override_variable_array_capacity:  False
//     cast_format:  (({type}) {value})
//     std:  c11

#ifndef R2_HOLO_MOTION_SPEC_0_1_INCLUDED_
#define R2_HOLO_MOTION_SPEC_0_1_INCLUDED_

#include <nunavut/support/serialization.h>
#include <stdint.h>
#include <stdlib.h>

static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_TARGET_ENDIANNESS == 1693710260,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_OMIT_FLOAT_SERIALIZATION_SUPPORT == 0,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_SERIALIZATION_ASSERTS == 0,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_OVERRIDE_VARIABLE_ARRAY_CAPACITY == 0,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_CAST_FORMAT == 2368206204,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_STD == 575908835,
              "external/r2_cyphal_dsdl/r2/holo/MotionSpec.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );

#ifdef __cplusplus
extern "C" {
#endif

/// This type does not have a fixed port-ID. See https://forum.opencyphal.org/t/choosing-message-and-service-ids/889
#define r2_holo_MotionSpec_0_1_HAS_FIXED_PORT_ID_ false

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.MotionSpec.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_MotionSpec_0_1_FULL_NAME_             "r2.holo.MotionSpec"
#define r2_holo_MotionSpec_0_1_FULL_NAME_AND_VERSION_ "r2.holo.MotionSpec.0.1"

/// Extent is the minimum amount of memory required to hold any serialized representation of any compatible
/// version of the data type; or, on other words, it is the the maximum possible size of received objects of this type.
/// The size is specified in bytes (rather than bits) because by definition, extent is an integer number of bytes long.
/// When allocating a deserialization (RX) buffer for this data type, it should be at least extent bytes large.
/// When allocating a serialization (TX) buffer, it is safe to use the size of the largest serialized representation
/// instead of the extent because it provides a tighter bound of the object size; it is safe because the concrete type
/// is always known during serialization (unlike deserialization). If not sure, use extent everywhere.
#define r2_holo_MotionSpec_0_1_EXTENT_BYTES_                    19UL
#define r2_holo_MotionSpec_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ 19UL
static_assert(r2_holo_MotionSpec_0_1_EXTENT_BYTES_ >= r2_holo_MotionSpec_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
              "Internal constraint violation");

/// saturated uint4 CENTER = 0
#define r2_holo_MotionSpec_0_1_CENTER (0U)

/// saturated uint4 MOVE = 1
#define r2_holo_MotionSpec_0_1_MOVE (1U)

/// saturated uint4 NUDGE = 2
#define r2_holo_MotionSpec_0_1_NUDGE (2U)

/// saturated uint4 TWITCH = 3
#define r2_holo_MotionSpec_0_1_TWITCH (3U)

/// saturated uint4 WAG = 4
#define r2_holo_MotionSpec_0_1_WAG (4U)

/// saturated uint4 NOD = 5
#define r2_holo_MotionSpec_0_1_NOD (5U)

/// saturated uint4 SCAN = 6
#define r2_holo_MotionSpec_0_1_SCAN (6U)

/// saturated uint4 CIRCLE = 7
#define r2_holo_MotionSpec_0_1_CIRCLE (7U)

/// saturated uint4 STOP = 8
#define r2_holo_MotionSpec_0_1_STOP (8U)

/// saturated uint4 OFF = 9
#define r2_holo_MotionSpec_0_1_OFF (9U)

/// Array metadata for: saturated uint16[<=1] duration_ms
#define r2_holo_MotionSpec_0_1_duration_ms_ARRAY_CAPACITY_           1U
#define r2_holo_MotionSpec_0_1_duration_ms_ARRAY_IS_VARIABLE_LENGTH_ true

typedef struct
{
    /// saturated uint4 kind
    uint8_t kind;

    /// saturated uint7 range
    uint8_t range;

    /// saturated uint5 count
    uint8_t count;

    /// saturated int16 x
    int16_t x;

    /// saturated int16 y
    int16_t y;

    /// saturated uint16 period_ms
    uint16_t period_ms;

    /// saturated uint20 interval_min_ms
    uint32_t interval_min_ms;

    /// saturated uint20 interval_max_ms
    uint32_t interval_max_ms;

    /// saturated uint22 time_limit_ms
    uint32_t time_limit_ms;

    /// saturated uint16[<=1] duration_ms
    struct  /// Array address equivalence guarantee: &elements[0] == &duration_ms
    {
        uint16_t elements[r2_holo_MotionSpec_0_1_duration_ms_ARRAY_CAPACITY_];
        size_t count;
    } duration_ms;
} r2_holo_MotionSpec_0_1;

/// Serialize an instance into the provided buffer.
/// The lifetime of the resulting serialized representation is independent of the original instance.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original object where possible.
///
/// @param obj      The object to serialize.
///
/// @param buffer   The destination buffer. There are no alignment requirements.
///                 @see r2_holo_MotionSpec_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the buffer in bytes.
///                                 Upon return this value will be updated with the size of the constructed serialized
///                                 representation (in bytes); this value is then to be passed over to the transport
///                                 layer. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_MotionSpec_0_1_serialize_(
    const r2_holo_MotionSpec_0_1* const obj, uint8_t* const buffer,  size_t* const inout_buffer_size_bytes)
{
    if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    if ((8U * (size_t) capacity_bytes) < 152UL)
    {
        return -NUNAVUT_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    // Notice that fields that are not an integer number of bytes long may overrun the space allocated for them
    // in the serialization buffer up to the next byte boundary. This is by design and is guaranteed to be safe.
    size_t offset_bits = 0U;
    {   // saturated uint4 kind
        uint8_t _sat0_ = obj->kind;
        if (_sat0_ > 15U)
        {
            _sat0_ = 15U;
        }
        buffer[offset_bits / 8U] = (uint8_t)(_sat0_);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 4U;
    }
    {   // saturated uint7 range
        uint8_t _sat1_ = obj->range;
        if (_sat1_ > 127U)
        {
            _sat1_ = 127U;
        }
        const int8_t _err0_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, _sat1_, 7U);
        if (_err0_ < 0)
        {
            return _err0_;
        }
        offset_bits += 7U;
    }
    {   // saturated uint5 count
        uint8_t _sat2_ = obj->count;
        if (_sat2_ > 31U)
        {
            _sat2_ = 31U;
        }
        const int8_t _err1_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, _sat2_, 5U);
        if (_err1_ < 0)
        {
            return _err1_;
        }
        offset_bits += 5U;
    }
    {   // saturated int16 x
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err2_ = nunavutSetIxx(&buffer[0], capacity_bytes, offset_bits, obj->x, 16U);
        if (_err2_ < 0)
        {
            return _err2_;
        }
        offset_bits += 16U;
    }
    {   // saturated int16 y
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err3_ = nunavutSetIxx(&buffer[0], capacity_bytes, offset_bits, obj->y, 16U);
        if (_err3_ < 0)
        {
            return _err3_;
        }
        offset_bits += 16U;
    }
    {   // saturated uint16 period_ms
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err4_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->period_ms, 16U);
        if (_err4_ < 0)
        {
            return _err4_;
        }
        offset_bits += 16U;
    }
    {   // saturated uint20 interval_min_ms
        uint32_t _sat3_ = obj->interval_min_ms;
        if (_sat3_ > 1048575UL)
        {
            _sat3_ = 1048575UL;
        }
        const int8_t _err5_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, _sat3_, 20U);
        if (_err5_ < 0)
        {
            return _err5_;
        }
        offset_bits += 20U;
    }
    {   // saturated uint20 interval_max_ms
        uint32_t _sat4_ = obj->interval_max_ms;
        if (_sat4_ > 1048575UL)
        {
            _sat4_ = 1048575UL;
        }
        const int8_t _err6_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, _sat4_, 20U);
        if (_err6_ < 0)
        {
            return _err6_;
        }
        offset_bits += 20U;
    }
    {   // saturated uint22 time_limit_ms
        uint32_t _sat5_ = obj->time_limit_ms;
        if (_sat5_ > 4194303UL)
        {
            _sat5_ = 4194303UL;
        }
        const int8_t _err7_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, _sat5_, 22U);
        if (_err7_ < 0)
        {
            return _err7_;
        }
        offset_bits += 22U;
    }
    {   // saturated uint16[<=1] duration_ms
        if (obj->duration_ms.count > 1)
        {
            return -NUNAVUT_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
        }
        // Array length prefix: truncated uint8
        const int8_t _err8_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->duration_ms.count, 8U);
        if (_err8_ < 0)
        {
            return _err8_;
        }
        offset_bits += 8U;
        for (size_t _index0_ = 0U; _index0_ < obj->duration_ms.count; ++_index0_)
        {
            // Saturation code not emitted -- native representation matches the serialized representation.
            const int8_t _err9_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->duration_ms.elements[_index0_], 16U);
            if (_err9_ < 0)
            {
                return _err9_;
            }
            offset_bits += 16U;
        }
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad0_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err10_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad0_);  // Optimize?
        if (_err10_ < 0)
        {
            return _err10_;
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
static inline int8_t r2_holo_MotionSpec_0_1_deserialize_(
    r2_holo_MotionSpec_0_1* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)
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
    // saturated uint4 kind
    if ((offset_bits + 4U) <= capacity_bits)
    {
        out_obj->kind = buffer[offset_bits / 8U] & 15U;
    }
    else
    {
        out_obj->kind = 0U;
    }
    offset_bits += 4U;
    // saturated uint7 range
    out_obj->range = nunavutGetU8(&buffer[0], capacity_bytes, offset_bits, 7);
    offset_bits += 7U;
    // saturated uint5 count
    out_obj->count = nunavutGetU8(&buffer[0], capacity_bytes, offset_bits, 5);
    offset_bits += 5U;
    // saturated int16 x
    out_obj->x = nunavutGetI16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated int16 y
    out_obj->y = nunavutGetI16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated uint16 period_ms
    out_obj->period_ms = nunavutGetU16(&buffer[0], capacity_bytes, offset_bits, 16);
    offset_bits += 16U;
    // saturated uint20 interval_min_ms
    out_obj->interval_min_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 20);
    offset_bits += 20U;
    // saturated uint20 interval_max_ms
    out_obj->interval_max_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 20);
    offset_bits += 20U;
    // saturated uint22 time_limit_ms
    out_obj->time_limit_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 22);
    offset_bits += 22U;
    // saturated uint16[<=1] duration_ms
    // Array length prefix: truncated uint8
    out_obj->duration_ms.count = nunavutGetU8(&buffer[0], capacity_bytes, offset_bits, 8);
    offset_bits += 8U;
    if (out_obj->duration_ms.count > 1U)
    {
        return -NUNAVUT_ERROR_REPRESENTATION_BAD_ARRAY_LENGTH;
    }
    for (size_t _index1_ = 0U; _index1_ < out_obj->duration_ms.count; ++_index1_)
    {
        out_obj->duration_ms.elements[_index1_] = nunavutGetU16(&buffer[0], capacity_bytes, offset_bits, 16);
        offset_bits += 16U;
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    *inout_buffer_size_bytes = (size_t) (nunavutChooseMin(offset_bits, capacity_bits) / 8U);
    return NUNAVUT_SUCCESS;
}

/// Initialize an instance to default values. Does nothing if @param out_obj is NULL.
/// This function intentionally leaves inactive elements uninitialized; for example, members of a variable-length
/// array beyond its length are left uninitialized; aliased union memory that is not used by the first union field
/// is left uninitialized, etc. If full zero-initialization is desired, just use memset(&obj, 0, sizeof(obj)).
static inline void r2_holo_MotionSpec_0_1_initialize_(r2_holo_MotionSpec_0_1* const out_obj)
{
    if (out_obj != NULL)
    {
        size_t size_bytes = 0;
        const uint8_t buf = 0;
        const int8_t err = r2_holo_MotionSpec_0_1_deserialize_(out_obj, &buf, &size_bytes);

        (void) err;
    }
}

#ifdef __cplusplus
}
#endif
#endif // R2_HOLO_MOTION_SPEC_0_1_INCLUDED_
