/*
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(OM_TYPE_METADATA_DEF)
// Data type metadata declared in the following format:
// OM_TYPE_METADATA_DEF( dtype enum name, dtype enum value, dtype size)
// dtype enum values are standard ONNX data types defined in
// https://github.com/onnx/onnx/blob/main/onnx/onnx.proto#L484
// clang-format off
OM_TYPE_METADATA_DEF(ONNX_TYPE_UNDEFINED,   0,  0)
OM_TYPE_METADATA_DEF(ONNX_TYPE_FLOAT,       1,  sizeof(float))
OM_TYPE_METADATA_DEF(ONNX_TYPE_UINT8,       2,  sizeof(uint8_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_INT8,        3,  sizeof(int8_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_UINT16,      4,  sizeof(uint16_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_INT16,       5,  sizeof(int16_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_INT32,       6,  sizeof(int32_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_INT64,       7,  sizeof(int64_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_STRING,      8,  0)
OM_TYPE_METADATA_DEF(ONNX_TYPE_BOOL,        9,  sizeof(bool))
OM_TYPE_METADATA_DEF(ONNX_TYPE_FLOAT16,     10, 2)
OM_TYPE_METADATA_DEF(ONNX_TYPE_DOUBLE,      11, sizeof(double))
OM_TYPE_METADATA_DEF(ONNX_TYPE_UINT32,      12, sizeof(uint32_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_UINT64,      13, sizeof(uint64_t))
OM_TYPE_METADATA_DEF(ONNX_TYPE_COMPLEX64,   14, 8)
OM_TYPE_METADATA_DEF(ONNX_TYPE_COMPLEX128,  15, 16)
OM_TYPE_METADATA_DEF(ONNX_TYPE_BFLOAT16,    16, 2)
// clang-format on
#else
#error "Must define OM_TYPE_METADATA_DEF macro."
#endif
