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
// Source file:   external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl
// Is deprecated: no
// Fixed port-ID: None
// Full name:     r2.holo.state.Holo
// Version:       0.1
//
// Language Options
//     target_endianness:  any
//     omit_float_serialization_support:  False
//     enable_serialization_asserts:  False
//     enable_override_variable_array_capacity:  False
//     cast_format:  (({type}) {value})
//     std:  c11

#ifndef R2_HOLO_STATE_HOLO_0_1_INCLUDED_
#define R2_HOLO_STATE_HOLO_0_1_INCLUDED_

#include <nunavut/support/serialization.h>
#include <r2/holo/MotionSpec_0_1.h>
#include <r2/holo/Reason_0_1.h>
#include <r2/holo/state/Axis_0_1.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_TARGET_ENDIANNESS == 1693710260,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_OMIT_FLOAT_SERIALIZATION_SUPPORT == 0,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_SERIALIZATION_ASSERTS == 0,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_OVERRIDE_VARIABLE_ARRAY_CAPACITY == 0,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_CAST_FORMAT == 2368206204,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_STD == 575908835,
              "external/r2_cyphal_dsdl/r2/holo/state/Holo.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );

#ifdef __cplusplus
extern "C" {
#endif

/// This type does not have a fixed port-ID. See https://forum.opencyphal.org/t/choosing-message-and-service-ids/889
#define r2_holo_state_Holo_0_1_HAS_FIXED_PORT_ID_ false

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.state.Holo.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_state_Holo_0_1_FULL_NAME_             "r2.holo.state.Holo"
#define r2_holo_state_Holo_0_1_FULL_NAME_AND_VERSION_ "r2.holo.state.Holo.0.1"

/// Extent is the minimum amount of memory required to hold any serialized representation of any compatible
/// version of the data type; or, on other words, it is the the maximum possible size of received objects of this type.
/// The size is specified in bytes (rather than bits) because by definition, extent is an integer number of bytes long.
/// When allocating a deserialization (RX) buffer for this data type, it should be at least extent bytes large.
/// When allocating a serialization (TX) buffer, it is safe to use the size of the largest serialized representation
/// instead of the extent because it provides a tighter bound of the object size; it is safe because the concrete type
/// is always known during serialization (unlike deserialization). If not sure, use extent everywhere.
#define r2_holo_state_Holo_0_1_EXTENT_BYTES_                    38UL
#define r2_holo_state_Holo_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ 38UL
static_assert(r2_holo_state_Holo_0_1_EXTENT_BYTES_ >= r2_holo_state_Holo_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
              "Internal constraint violation");

/// saturated uint32 NEVER = 4294967295
#define r2_holo_state_Holo_0_1_NEVER (4294967295UL)

/// saturated uint8 HOLD = 0
#define r2_holo_state_Holo_0_1_HOLD (0U)

/// saturated uint8 MOVE = 1
#define r2_holo_state_Holo_0_1_MOVE (1U)

/// saturated uint8 TWITCH = 2
#define r2_holo_state_Holo_0_1_TWITCH (2U)

/// saturated uint8 WAG = 3
#define r2_holo_state_Holo_0_1_WAG (3U)

/// saturated uint8 NOD = 4
#define r2_holo_state_Holo_0_1_NOD (4U)

/// saturated uint8 SCAN = 5
#define r2_holo_state_Holo_0_1_SCAN (5U)

/// saturated uint8 CIRCLE = 6
#define r2_holo_state_Holo_0_1_CIRCLE (6U)

typedef struct
{
    /// bool ready
    bool ready;

    /// r2.holo.Reason.0.1 why
    r2_holo_Reason_0_1 why;

    /// saturated uint8 motion
    uint8_t motion;

    /// saturated uint32 motion_age_ms
    uint32_t motion_age_ms;

    /// saturated uint32 motion_remaining_ms
    uint32_t motion_remaining_ms;

    /// r2.holo.Reason.0.1 stopped
    r2_holo_Reason_0_1 stopped;

    /// saturated uint8 stopped_motion
    uint8_t stopped_motion;

    /// saturated uint32 stopped_age_ms
    uint32_t stopped_age_ms;

    /// r2.holo.state.Axis.0.1 horizontal
    r2_holo_state_Axis_0_1 horizontal;

    /// r2.holo.state.Axis.0.1 vertical
    r2_holo_state_Axis_0_1 vertical;

    /// r2.holo.MotionSpec.0.1 last
    r2_holo_MotionSpec_0_1 last;
} r2_holo_state_Holo_0_1;

/// Serialize an instance into the provided buffer.
/// The lifetime of the resulting serialized representation is independent of the original instance.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original object where possible.
///
/// @param obj      The object to serialize.
///
/// @param buffer   The destination buffer. There are no alignment requirements.
///                 @see r2_holo_state_Holo_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the buffer in bytes.
///                                 Upon return this value will be updated with the size of the constructed serialized
///                                 representation (in bytes); this value is then to be passed over to the transport
///                                 layer. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_state_Holo_0_1_serialize_(
    const r2_holo_state_Holo_0_1* const obj, uint8_t* const buffer,  size_t* const inout_buffer_size_bytes)
{
    if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    if ((8U * (size_t) capacity_bytes) < 304UL)
    {
        return -NUNAVUT_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    // Notice that fields that are not an integer number of bytes long may overrun the space allocated for them
    // in the serialization buffer up to the next byte boundary. This is by design and is guaranteed to be safe.
    size_t offset_bits = 0U;
    {   // bool ready
        buffer[offset_bits / 8U] = obj->ready ? 1U : 0U;
        offset_bits += 1U;
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad0_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err0_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad0_);  // Optimize?
        if (_err0_ < 0)
        {
            return _err0_;
        }
        offset_bits += _pad0_;
    }
    {   // r2.holo.Reason.0.1 why
        size_t _size_bytes0_ = 1UL;  // Nested object (max) size, in bytes.
        int8_t _err1_ = r2_holo_Reason_0_1_serialize_(
            &obj->why, &buffer[offset_bits / 8U], &_size_bytes0_);
        if (_err1_ < 0)
        {
            return _err1_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes0_ * 8U;  // Advance by the size of the nested object.
    }
    {   // saturated uint8 motion
        // Saturation code not emitted -- native representation matches the serialized representation.
        buffer[offset_bits / 8U] = (uint8_t)(obj->motion);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 8U;
    }
    {   // saturated uint32 motion_age_ms
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err2_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->motion_age_ms, 32U);
        if (_err2_ < 0)
        {
            return _err2_;
        }
        offset_bits += 32U;
    }
    {   // saturated uint32 motion_remaining_ms
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err3_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->motion_remaining_ms, 32U);
        if (_err3_ < 0)
        {
            return _err3_;
        }
        offset_bits += 32U;
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad1_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err4_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad1_);  // Optimize?
        if (_err4_ < 0)
        {
            return _err4_;
        }
        offset_bits += _pad1_;
    }
    {   // r2.holo.Reason.0.1 stopped
        size_t _size_bytes1_ = 1UL;  // Nested object (max) size, in bytes.
        int8_t _err5_ = r2_holo_Reason_0_1_serialize_(
            &obj->stopped, &buffer[offset_bits / 8U], &_size_bytes1_);
        if (_err5_ < 0)
        {
            return _err5_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes1_ * 8U;  // Advance by the size of the nested object.
    }
    {   // saturated uint8 stopped_motion
        // Saturation code not emitted -- native representation matches the serialized representation.
        buffer[offset_bits / 8U] = (uint8_t)(obj->stopped_motion);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 8U;
    }
    {   // saturated uint32 stopped_age_ms
        // Saturation code not emitted -- native representation matches the serialized representation.
        const int8_t _err6_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, obj->stopped_age_ms, 32U);
        if (_err6_ < 0)
        {
            return _err6_;
        }
        offset_bits += 32U;
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad2_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err7_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad2_);  // Optimize?
        if (_err7_ < 0)
        {
            return _err7_;
        }
        offset_bits += _pad2_;
    }
    {   // r2.holo.state.Axis.0.1 horizontal
        size_t _size_bytes2_ = 1UL;  // Nested object (max) size, in bytes.
        int8_t _err8_ = r2_holo_state_Axis_0_1_serialize_(
            &obj->horizontal, &buffer[offset_bits / 8U], &_size_bytes2_);
        if (_err8_ < 0)
        {
            return _err8_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes2_ * 8U;  // Advance by the size of the nested object.
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad3_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err9_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad3_);  // Optimize?
        if (_err9_ < 0)
        {
            return _err9_;
        }
        offset_bits += _pad3_;
    }
    {   // r2.holo.state.Axis.0.1 vertical
        size_t _size_bytes3_ = 1UL;  // Nested object (max) size, in bytes.
        int8_t _err10_ = r2_holo_state_Axis_0_1_serialize_(
            &obj->vertical, &buffer[offset_bits / 8U], &_size_bytes3_);
        if (_err10_ < 0)
        {
            return _err10_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes3_ * 8U;  // Advance by the size of the nested object.
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad4_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err11_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad4_);  // Optimize?
        if (_err11_ < 0)
        {
            return _err11_;
        }
        offset_bits += _pad4_;
    }
    {   // r2.holo.MotionSpec.0.1 last
        size_t _size_bytes4_ = 19UL;  // Nested object (max) size, in bytes.
        int8_t _err12_ = r2_holo_MotionSpec_0_1_serialize_(
            &obj->last, &buffer[offset_bits / 8U], &_size_bytes4_);
        if (_err12_ < 0)
        {
            return _err12_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes4_ * 8U;  // Advance by the size of the nested object.
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad5_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err13_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad5_);  // Optimize?
        if (_err13_ < 0)
        {
            return _err13_;
        }
        offset_bits += _pad5_;
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
static inline int8_t r2_holo_state_Holo_0_1_deserialize_(
    r2_holo_state_Holo_0_1* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)
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
    // bool ready
    if (offset_bits < capacity_bits)
    {
        out_obj->ready = (buffer[offset_bits / 8U] & 1U) != 0U;
    }
    else
    {
        out_obj->ready = false;
    }
    offset_bits += 1U;
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    // r2.holo.Reason.0.1 why
    {
        size_t _size_bytes5_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err14_ = r2_holo_Reason_0_1_deserialize_(
            &out_obj->why, &buffer[offset_bits / 8U], &_size_bytes5_);
        if (_err14_ < 0)
        {
            return _err14_;
        }
        offset_bits += _size_bytes5_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    // saturated uint8 motion
    if ((offset_bits + 8U) <= capacity_bits)
    {
        out_obj->motion = buffer[offset_bits / 8U] & 255U;
    }
    else
    {
        out_obj->motion = 0U;
    }
    offset_bits += 8U;
    // saturated uint32 motion_age_ms
    out_obj->motion_age_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 32);
    offset_bits += 32U;
    // saturated uint32 motion_remaining_ms
    out_obj->motion_remaining_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 32);
    offset_bits += 32U;
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    // r2.holo.Reason.0.1 stopped
    {
        size_t _size_bytes6_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err15_ = r2_holo_Reason_0_1_deserialize_(
            &out_obj->stopped, &buffer[offset_bits / 8U], &_size_bytes6_);
        if (_err15_ < 0)
        {
            return _err15_;
        }
        offset_bits += _size_bytes6_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    // saturated uint8 stopped_motion
    if ((offset_bits + 8U) <= capacity_bits)
    {
        out_obj->stopped_motion = buffer[offset_bits / 8U] & 255U;
    }
    else
    {
        out_obj->stopped_motion = 0U;
    }
    offset_bits += 8U;
    // saturated uint32 stopped_age_ms
    out_obj->stopped_age_ms = nunavutGetU32(&buffer[0], capacity_bytes, offset_bits, 32);
    offset_bits += 32U;
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    // r2.holo.state.Axis.0.1 horizontal
    {
        size_t _size_bytes7_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err16_ = r2_holo_state_Axis_0_1_deserialize_(
            &out_obj->horizontal, &buffer[offset_bits / 8U], &_size_bytes7_);
        if (_err16_ < 0)
        {
            return _err16_;
        }
        offset_bits += _size_bytes7_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    // r2.holo.state.Axis.0.1 vertical
    {
        size_t _size_bytes8_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err17_ = r2_holo_state_Axis_0_1_deserialize_(
            &out_obj->vertical, &buffer[offset_bits / 8U], &_size_bytes8_);
        if (_err17_ < 0)
        {
            return _err17_;
        }
        offset_bits += _size_bytes8_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    // r2.holo.MotionSpec.0.1 last
    {
        size_t _size_bytes9_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
        const int8_t _err18_ = r2_holo_MotionSpec_0_1_deserialize_(
            &out_obj->last, &buffer[offset_bits / 8U], &_size_bytes9_);
        if (_err18_ < 0)
        {
            return _err18_;
        }
        offset_bits += _size_bytes9_ * 8U;  // Advance by the size of the nested serialized representation.
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    *inout_buffer_size_bytes = (size_t) (nunavutChooseMin(offset_bits, capacity_bits) / 8U);
    return NUNAVUT_SUCCESS;
}

/// Initialize an instance to default values. Does nothing if @param out_obj is NULL.
/// This function intentionally leaves inactive elements uninitialized; for example, members of a variable-length
/// array beyond its length are left uninitialized; aliased union memory that is not used by the first union field
/// is left uninitialized, etc. If full zero-initialization is desired, just use memset(&obj, 0, sizeof(obj)).
static inline void r2_holo_state_Holo_0_1_initialize_(r2_holo_state_Holo_0_1* const out_obj)
{
    if (out_obj != NULL)
    {
        size_t size_bytes = 0;
        const uint8_t buf = 0;
        const int8_t err = r2_holo_state_Holo_0_1_deserialize_(out_obj, &buf, &size_bytes);

        (void) err;
    }
}

#ifdef __cplusplus
}
#endif
#endif // R2_HOLO_STATE_HOLO_0_1_INCLUDED_
