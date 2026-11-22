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
// Source file:   external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl
// Is deprecated: no
// Fixed port-ID: None
// Full name:     r2.holo.events.Happening
// Version:       0.1
//
// Language Options
//     target_endianness:  any
//     omit_float_serialization_support:  False
//     enable_serialization_asserts:  False
//     enable_override_variable_array_capacity:  False
//     cast_format:  (({type}) {value})
//     std:  c11

#ifndef R2_HOLO_EVENTS_HAPPENING_0_1_INCLUDED_
#define R2_HOLO_EVENTS_HAPPENING_0_1_INCLUDED_

#include <nunavut/support/serialization.h>
#include <r2/holo/events/ClipEnded_0_1.h>
#include <r2/holo/events/ClipStarted_0_1.h>
#include <r2/holo/events/SceneEnded_0_1.h>
#include <r2/holo/events/SceneStarted_0_1.h>
#include <r2/holo/events/Touch_0_1.h>
#include <stdint.h>
#include <stdlib.h>

static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_TARGET_ENDIANNESS == 1693710260,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_OMIT_FLOAT_SERIALIZATION_SUPPORT == 0,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_SERIALIZATION_ASSERTS == 0,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_ENABLE_OVERRIDE_VARIABLE_ARRAY_CAPACITY == 0,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_CAST_FORMAT == 2368206204,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );
static_assert( NUNAVUT_SUPPORT_LANGUAGE_OPTION_STD == 575908835,
              "external/r2_cyphal_dsdl/r2/holo/events/Happening.0.1.dsdl is trying to use a serialization library that was compiled with "
              "different language options. This is dangerous and therefore not allowed." );

#ifdef __cplusplus
extern "C" {
#endif

/// This type does not have a fixed port-ID. See https://forum.opencyphal.org/t/choosing-message-and-service-ids/889
#define r2_holo_events_Happening_0_1_HAS_FIXED_PORT_ID_ false

// +-------------------------------------------------------------------------------------------------------------------+
// | r2.holo.events.Happening.0.1
// +-------------------------------------------------------------------------------------------------------------------+
#define r2_holo_events_Happening_0_1_FULL_NAME_             "r2.holo.events.Happening"
#define r2_holo_events_Happening_0_1_FULL_NAME_AND_VERSION_ "r2.holo.events.Happening.0.1"

/// Extent is the minimum amount of memory required to hold any serialized representation of any compatible
/// version of the data type; or, on other words, it is the the maximum possible size of received objects of this type.
/// The size is specified in bytes (rather than bits) because by definition, extent is an integer number of bytes long.
/// When allocating a deserialization (RX) buffer for this data type, it should be at least extent bytes large.
/// When allocating a serialization (TX) buffer, it is safe to use the size of the largest serialized representation
/// instead of the extent because it provides a tighter bound of the object size; it is safe because the concrete type
/// is always known during serialization (unlike deserialization). If not sure, use extent everywhere.
#define r2_holo_events_Happening_0_1_EXTENT_BYTES_                    263UL
#define r2_holo_events_Happening_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_ 263UL
static_assert(r2_holo_events_Happening_0_1_EXTENT_BYTES_ >= r2_holo_events_Happening_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_,
              "Internal constraint violation");

typedef struct
{
    union  /// The union is placed first to ensure that the active element address equals the struct address.
    {
        /// r2.holo.events.ClipStarted.0.1 clip_started
        r2_holo_events_ClipStarted_0_1 clip_started;

        /// r2.holo.events.ClipEnded.0.1 clip_ended
        r2_holo_events_ClipEnded_0_1 clip_ended;

        /// r2.holo.events.SceneStarted.0.1 scene_started
        r2_holo_events_SceneStarted_0_1 scene_started;

        /// r2.holo.events.SceneEnded.0.1 scene_ended
        r2_holo_events_SceneEnded_0_1 scene_ended;

        /// r2.holo.events.Touch.0.1 screen_touch
        r2_holo_events_Touch_0_1 screen_touch;
    };
    uint8_t _tag_;
} r2_holo_events_Happening_0_1;

/// The number of fields in the union. Valid tag values range from zero to this value minus one, inclusive.
#define r2_holo_events_Happening_0_1_UNION_OPTION_COUNT_ 5U

/// Serialize an instance into the provided buffer.
/// The lifetime of the resulting serialized representation is independent of the original instance.
/// This method may be slow for large objects (e.g., images, point clouds, radar samples), so in a later revision
/// we may define a zero-copy alternative that keeps references to the original object where possible.
///
/// @param obj      The object to serialize.
///
/// @param buffer   The destination buffer. There are no alignment requirements.
///                 @see r2_holo_events_Happening_0_1_SERIALIZATION_BUFFER_SIZE_BYTES_
///
/// @param inout_buffer_size_bytes  When calling, this is a pointer to the size of the buffer in bytes.
///                                 Upon return this value will be updated with the size of the constructed serialized
///                                 representation (in bytes); this value is then to be passed over to the transport
///                                 layer. In case of error this value is undefined.
///
/// @returns Negative on error, zero on success.
static inline int8_t r2_holo_events_Happening_0_1_serialize_(
    const r2_holo_events_Happening_0_1* const obj, uint8_t* const buffer,  size_t* const inout_buffer_size_bytes)
{
    if ((obj == NULL) || (buffer == NULL) || (inout_buffer_size_bytes == NULL))
    {
        return -NUNAVUT_ERROR_INVALID_ARGUMENT;
    }
    const size_t capacity_bytes = *inout_buffer_size_bytes;
    if ((8U * (size_t) capacity_bytes) < 2104UL)
    {
        return -NUNAVUT_ERROR_SERIALIZATION_BUFFER_TOO_SMALL;
    }
    // Notice that fields that are not an integer number of bytes long may overrun the space allocated for them
    // in the serialization buffer up to the next byte boundary. This is by design and is guaranteed to be safe.
    size_t offset_bits = 0U;
    {   // Union tag field: truncated uint8
        buffer[offset_bits / 8U] = (uint8_t)(obj->_tag_);  // C std, 6.3.1.3 Signed and unsigned integers
        offset_bits += 8U;
    }
    if (0U == obj->_tag_)  // r2.holo.events.ClipStarted.0.1 clip_started
    {
        size_t _size_bytes0_ = 262UL;  // Nested object (max) size, in bytes.
        int8_t _err0_ = r2_holo_events_ClipStarted_0_1_serialize_(
            &obj->clip_started, &buffer[offset_bits / 8U], &_size_bytes0_);
        if (_err0_ < 0)
        {
            return _err0_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes0_ * 8U;  // Advance by the size of the nested object.
    }
    else if (1U == obj->_tag_)  // r2.holo.events.ClipEnded.0.1 clip_ended
    {
        size_t _size_bytes1_ = 257UL;  // Nested object (max) size, in bytes.
        int8_t _err1_ = r2_holo_events_ClipEnded_0_1_serialize_(
            &obj->clip_ended, &buffer[offset_bits / 8U], &_size_bytes1_);
        if (_err1_ < 0)
        {
            return _err1_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes1_ * 8U;  // Advance by the size of the nested object.
    }
    else if (2U == obj->_tag_)  // r2.holo.events.SceneStarted.0.1 scene_started
    {
        size_t _size_bytes2_ = 40UL;  // Nested object (max) size, in bytes.
        int8_t _err2_ = r2_holo_events_SceneStarted_0_1_serialize_(
            &obj->scene_started, &buffer[offset_bits / 8U], &_size_bytes2_);
        if (_err2_ < 0)
        {
            return _err2_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes2_ * 8U;  // Advance by the size of the nested object.
    }
    else if (3U == obj->_tag_)  // r2.holo.events.SceneEnded.0.1 scene_ended
    {
        size_t _size_bytes3_ = 36UL;  // Nested object (max) size, in bytes.
        int8_t _err3_ = r2_holo_events_SceneEnded_0_1_serialize_(
            &obj->scene_ended, &buffer[offset_bits / 8U], &_size_bytes3_);
        if (_err3_ < 0)
        {
            return _err3_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes3_ * 8U;  // Advance by the size of the nested object.
    }
    else if (4U == obj->_tag_)  // r2.holo.events.Touch.0.1 screen_touch
    {
        size_t _size_bytes4_ = 3UL;  // Nested object (max) size, in bytes.
        int8_t _err4_ = r2_holo_events_Touch_0_1_serialize_(
            &obj->screen_touch, &buffer[offset_bits / 8U], &_size_bytes4_);
        if (_err4_ < 0)
        {
            return _err4_;
        }
        // It is assumed that we know the exact type of the serialized entity, hence we expect the size to match.
        offset_bits += _size_bytes4_ * 8U;  // Advance by the size of the nested object.
    }
    else
    {
        return -NUNAVUT_ERROR_REPRESENTATION_BAD_UNION_TAG;
    }
    if (offset_bits % 8U != 0U)  // Pad to 8 bits. TODO: Eliminate redundant padding checks.
    {
        const uint8_t _pad0_ = (uint8_t)(8U - offset_bits % 8U);
        const int8_t _err5_ = nunavutSetUxx(&buffer[0], capacity_bytes, offset_bits, 0U, _pad0_);  // Optimize?
        if (_err5_ < 0)
        {
            return _err5_;
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
static inline int8_t r2_holo_events_Happening_0_1_deserialize_(
    r2_holo_events_Happening_0_1* const out_obj, const uint8_t* buffer, size_t* const inout_buffer_size_bytes)
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
    // Union tag field: truncated uint8
    if ((offset_bits + 8U) <= capacity_bits)
    {
        out_obj->_tag_ = buffer[offset_bits / 8U] & 255U;
    }
    else
    {
        out_obj->_tag_ = 0U;
    }
    offset_bits += 8U;
    if (0U == out_obj->_tag_)  // r2.holo.events.ClipStarted.0.1 clip_started
    {
        {
            size_t _size_bytes5_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
            const int8_t _err6_ = r2_holo_events_ClipStarted_0_1_deserialize_(
                &out_obj->clip_started, &buffer[offset_bits / 8U], &_size_bytes5_);
            if (_err6_ < 0)
            {
                return _err6_;
            }
            offset_bits += _size_bytes5_ * 8U;  // Advance by the size of the nested serialized representation.
        }
    }
    else if (1U == out_obj->_tag_)  // r2.holo.events.ClipEnded.0.1 clip_ended
    {
        {
            size_t _size_bytes6_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
            const int8_t _err7_ = r2_holo_events_ClipEnded_0_1_deserialize_(
                &out_obj->clip_ended, &buffer[offset_bits / 8U], &_size_bytes6_);
            if (_err7_ < 0)
            {
                return _err7_;
            }
            offset_bits += _size_bytes6_ * 8U;  // Advance by the size of the nested serialized representation.
        }
    }
    else if (2U == out_obj->_tag_)  // r2.holo.events.SceneStarted.0.1 scene_started
    {
        {
            size_t _size_bytes7_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
            const int8_t _err8_ = r2_holo_events_SceneStarted_0_1_deserialize_(
                &out_obj->scene_started, &buffer[offset_bits / 8U], &_size_bytes7_);
            if (_err8_ < 0)
            {
                return _err8_;
            }
            offset_bits += _size_bytes7_ * 8U;  // Advance by the size of the nested serialized representation.
        }
    }
    else if (3U == out_obj->_tag_)  // r2.holo.events.SceneEnded.0.1 scene_ended
    {
        {
            size_t _size_bytes8_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
            const int8_t _err9_ = r2_holo_events_SceneEnded_0_1_deserialize_(
                &out_obj->scene_ended, &buffer[offset_bits / 8U], &_size_bytes8_);
            if (_err9_ < 0)
            {
                return _err9_;
            }
            offset_bits += _size_bytes8_ * 8U;  // Advance by the size of the nested serialized representation.
        }
    }
    else if (4U == out_obj->_tag_)  // r2.holo.events.Touch.0.1 screen_touch
    {
        {
            size_t _size_bytes9_ = (size_t)(capacity_bytes - nunavutChooseMin((offset_bits / 8U), capacity_bytes));
            const int8_t _err10_ = r2_holo_events_Touch_0_1_deserialize_(
                &out_obj->screen_touch, &buffer[offset_bits / 8U], &_size_bytes9_);
            if (_err10_ < 0)
            {
                return _err10_;
            }
            offset_bits += _size_bytes9_ * 8U;  // Advance by the size of the nested serialized representation.
        }
    }
    else
    {
        return -NUNAVUT_ERROR_REPRESENTATION_BAD_UNION_TAG;
    }
    offset_bits = (offset_bits + 7U) & ~(size_t) 7U;  // Align on 8 bits.
    *inout_buffer_size_bytes = (size_t) (nunavutChooseMin(offset_bits, capacity_bits) / 8U);
    return NUNAVUT_SUCCESS;
}

/// Initialize an instance to default values. Does nothing if @param out_obj is NULL.
/// This function intentionally leaves inactive elements uninitialized; for example, members of a variable-length
/// array beyond its length are left uninitialized; aliased union memory that is not used by the first union field
/// is left uninitialized, etc. If full zero-initialization is desired, just use memset(&obj, 0, sizeof(obj)).
static inline void r2_holo_events_Happening_0_1_initialize_(r2_holo_events_Happening_0_1* const out_obj)
{
    if (out_obj != NULL)
    {
        size_t size_bytes = 0;
        const uint8_t buf = 0;
        const int8_t err = r2_holo_events_Happening_0_1_deserialize_(out_obj, &buf, &size_bytes);

        (void) err;
    }
}
/// Mark option "clip_started" active without initializing it. Does nothing if @param obj is NULL.
static inline void r2_holo_events_Happening_0_1_select_clip_started_(r2_holo_events_Happening_0_1* const obj)
{
    if (obj != NULL)
    {
        obj->_tag_ = 0;
    }
}

/// Check if option "clip_started" is active. Returns false if @param obj is NULL.
static inline bool r2_holo_events_Happening_0_1_is_clip_started_(const r2_holo_events_Happening_0_1* const obj)
{
    return ((obj != NULL) && (obj->_tag_ == 0));
}

/// Mark option "clip_ended" active without initializing it. Does nothing if @param obj is NULL.
static inline void r2_holo_events_Happening_0_1_select_clip_ended_(r2_holo_events_Happening_0_1* const obj)
{
    if (obj != NULL)
    {
        obj->_tag_ = 1;
    }
}

/// Check if option "clip_ended" is active. Returns false if @param obj is NULL.
static inline bool r2_holo_events_Happening_0_1_is_clip_ended_(const r2_holo_events_Happening_0_1* const obj)
{
    return ((obj != NULL) && (obj->_tag_ == 1));
}

/// Mark option "scene_started" active without initializing it. Does nothing if @param obj is NULL.
static inline void r2_holo_events_Happening_0_1_select_scene_started_(r2_holo_events_Happening_0_1* const obj)
{
    if (obj != NULL)
    {
        obj->_tag_ = 2;
    }
}

/// Check if option "scene_started" is active. Returns false if @param obj is NULL.
static inline bool r2_holo_events_Happening_0_1_is_scene_started_(const r2_holo_events_Happening_0_1* const obj)
{
    return ((obj != NULL) && (obj->_tag_ == 2));
}

/// Mark option "scene_ended" active without initializing it. Does nothing if @param obj is NULL.
static inline void r2_holo_events_Happening_0_1_select_scene_ended_(r2_holo_events_Happening_0_1* const obj)
{
    if (obj != NULL)
    {
        obj->_tag_ = 3;
    }
}

/// Check if option "scene_ended" is active. Returns false if @param obj is NULL.
static inline bool r2_holo_events_Happening_0_1_is_scene_ended_(const r2_holo_events_Happening_0_1* const obj)
{
    return ((obj != NULL) && (obj->_tag_ == 3));
}

/// Mark option "screen_touch" active without initializing it. Does nothing if @param obj is NULL.
static inline void r2_holo_events_Happening_0_1_select_screen_touch_(r2_holo_events_Happening_0_1* const obj)
{
    if (obj != NULL)
    {
        obj->_tag_ = 4;
    }
}

/// Check if option "screen_touch" is active. Returns false if @param obj is NULL.
static inline bool r2_holo_events_Happening_0_1_is_screen_touch_(const r2_holo_events_Happening_0_1* const obj)
{
    return ((obj != NULL) && (obj->_tag_ == 4));
}

#ifdef __cplusplus
}
#endif
#endif // R2_HOLO_EVENTS_HAPPENING_0_1_INCLUDED_

