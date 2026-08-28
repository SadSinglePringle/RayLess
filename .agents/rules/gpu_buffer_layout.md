---
trigger: always_on
description: Enforces exact byte-level layout agreement between C++ and HLSL GPU buffers.
---

# GPU Buffer Layout Invariants

When defining or modifying data structures shared between C++ host code and HLSL compute shaders (via StructuredBuffers, ByteAddressBuffers, or ConstantBuffers):

1. **Exact Byte-for-Byte Agreement**: Field order, padding, and alignments must match identically between C++ struct definitions and HLSL struct declarations.
2. **Compile-Time Static Asserts**: Every GPU-shared struct in C++ must have comprehensive static asserts validating:
   - `sizeof(Struct) == EXPECTED_SIZE`
   - `alignof(Struct) == EXPECTED_ALIGNMENT`
   - `offsetof(Struct, member) == EXPECTED_OFFSET` for every member field.
3. **HLSL 16-Byte Vector Alignment**: Explicitly account for HLSL packing rules (16-byte boundary alignment for float4 / matrix rows) on both host and device.
