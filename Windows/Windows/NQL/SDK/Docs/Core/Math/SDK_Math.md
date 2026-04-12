<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Math

---

# SDK Math Documentation

Comprehensive documentation for the SDK Math utilities providing 3D vector and matrix operations for rendering transforms.

**Module:** `SDK/Core/Math/`  
**Output:** `SDK/Docs/Core/Math/SDK_Math.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Coordinate System](#coordinate-system)
- [Vector Operations](#vector-operations)
- [Matrix Operations](#matrix-operations)
- [Common Transform Patterns](#common-transform-patterns)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Math module provides fundamental 3D mathematics operations for game rendering and simulation. It implements vector algebra and 4x4 matrix transformations optimized for HLSL shader compatibility.

**Key Features:**
- 3D vector operations (add, subtract, scale, dot, cross, length, normalize)
- 4x4 matrix operations (identity, multiply, translation, scaling, rotation)
- Camera transforms (perspective projection, look-at view)
- Column-major matrix layout for GPU compatibility
- Left-handed coordinate system (+Z forward)
- Pure C implementation, no dependencies

---

## Coordinate System

### Matrix Layout

**Column-major order** (HLSL/DirectX convention):

```
SdkMat4.m[col][row]

Index layout (0-based):
    |  0   1   2   3  |      | m00 m01 m02 m03 |
col=0 | m00 m01 m02 m03 |  or  | m10 m11 m12 m13 |
col=1 | m10 m11 m12 m13 |      | m20 m21 m22 m23 |
col=2 | m20 m21 m22 m23 |      | m30 m31 m32 m33 |
col=3 | m30 m31 m32 m33 |

Memory layout (column-major):
    [m00, m10, m20, m30, m01, m11, m21, m31, m02, m12, m22, m32, m03, m13, m23, m33]
```

### Handedness

**Left-handed coordinate system:**
- +X: Right
- +Y: Up
- +Z: Forward (into the screen)

**Implications:**
- Cross product: `X × Y = Z`
- Look-at: +Z points toward target
- Perspective: clip space Z in [0, 1]

### Types

```c
// From sdk_types.h
typedef struct {
    float x, y, z;
} SdkVec3;

typedef struct {
    float m[4][4];  // [col][row]
} SdkMat4;

// Constants
#define SDK_DEG2RAD  (3.14159265358979323846f / 180.0f)
```

---

## Vector Operations

### Function Summary

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_vec3` | `(float x, float y, float z) → SdkVec3` | Create vector |
| `sdk_vec3_add` | `(SdkVec3 a, SdkVec3 b) → SdkVec3` | Component-wise addition |
| `sdk_vec3_sub` | `(SdkVec3 a, SdkVec3 b) → SdkVec3` | Component-wise subtraction |
| `sdk_vec3_scale` | `(SdkVec3 v, float s) → SdkVec3` | Scalar multiplication |
| `sdk_vec3_dot` | `(SdkVec3 a, SdkVec3 b) → float` | Dot product |
| `sdk_vec3_cross` | `(SdkVec3 a, SdkVec3 b) → SdkVec3` | Cross product |
| `sdk_vec3_length` | `(SdkVec3 v) → float` | Euclidean length |
| `sdk_vec3_normalize` | `(SdkVec3 v) → SdkVec3` | Unit vector (safe) |

### Operation Details

**Vector Creation:**
```c
SdkVec3 v = sdk_vec3(1.0f, 2.0f, 3.0f);
// v.x = 1.0, v.y = 2.0, v.z = 3.0
```

**Addition/Subtraction:**
```c
SdkVec3 a = sdk_vec3(1, 2, 3);
SdkVec3 b = sdk_vec3(4, 5, 6);
SdkVec3 sum = sdk_vec3_add(a, b);      // (5, 7, 9)
SdkVec3 diff = sdk_vec3_sub(a, b);     // (-3, -3, -3)
```

**Scaling:**
```c
SdkVec3 v = sdk_vec3(1, 2, 3);
SdkVec3 doubled = sdk_vec3_scale(v, 2.0f);  // (2, 4, 6)
```

**Dot Product:**
```c
SdkVec3 a = sdk_vec3(1, 0, 0);
SdkVec3 b = sdk_vec3(0, 1, 0);
float dot = sdk_vec3_dot(a, b);  // 0 (perpendicular)

// Uses: angle calculation, projection, lighting
float angle = acosf(sdk_vec3_dot(a, b) / (sdk_vec3_length(a) * sdk_vec3_length(b)));
```

**Cross Product:**
```c
SdkVec3 x = sdk_vec3(1, 0, 0);
SdkVec3 y = sdk_vec3(0, 1, 0);
SdkVec3 z = sdk_vec3_cross(x, y);  // (0, 0, 1)

// Uses: perpendicular vector, normal calculation, handedness
// |cross(a,b)| = |a||b|sin(angle)
```

**Length & Normalization:**
```c
SdkVec3 v = sdk_vec3(3, 4, 0);
float len = sdk_vec3_length(v);     // 5.0

SdkVec3 unit = sdk_vec3_normalize(v);  // (0.6, 0.8, 0.0)

// Safe normalization returns (0,0,0) for zero-length input
SdkVec3 zero = sdk_vec3(0, 0, 0);
SdkVec3 n = sdk_vec3_normalize(zero);  // (0, 0, 0)
```

---

## Matrix Operations

### Function Summary

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_mat4_identity` | `(SdkMat4* m) → void` | Set to identity |
| `sdk_mat4_multiply` | `(SdkMat4* out, const SdkMat4* a, const SdkMat4* b) → void` | Matrix multiply |
| `sdk_mat4_translation` | `(SdkMat4* m, SdkVec3 t) → void` | Translation matrix |
| `sdk_mat4_scaling` | `(SdkMat4* m, SdkVec3 s) → void` | Scale matrix |
| `sdk_mat4_rotation_x` | `(SdkMat4* m, float angle_rad) → void` | X-axis rotation |
| `sdk_mat4_rotation_y` | `(SdkMat4* m, float angle_rad) → void` | Y-axis rotation |
| `sdk_mat4_rotation_z` | `(SdkMat4* m, float angle_rad) → void` | Z-axis rotation |
| `sdk_mat4_perspective` | `(SdkMat4* m, float fov_deg, float aspect, float near, float far) → void` | Perspective projection |
| `sdk_mat4_look_at` | `(SdkMat4* m, SdkVec3 eye, SdkVec3 target, SdkVec3 up) → void` | View matrix |

### Operation Details

**Identity Matrix:**
```c
SdkMat4 m;
sdk_mat4_identity(&m);
// m = | 1 0 0 0 |
//     | 0 1 0 0 |
//     | 0 0 1 0 |
//     | 0 0 0 1 |
```

**Matrix Multiplication:**
```c
SdkMat4 a, b, result;
sdk_mat4_identity(&a);
sdk_mat4_translation(&b, sdk_vec3(1, 2, 3));
sdk_mat4_multiply(&result, &a, &b);  // result = a * b

// Note: Multiplies a * b (b applied first, then a)
// For transform chain: M = P * V * W (world, view, proj)
```

**Translation:**
```c
SdkMat4 m;
sdk_mat4_translation(&m, sdk_vec3(10, 20, 30));
// m = | 1 0 0 0 |
//     | 0 1 0 0 |
//     | 0 0 1 0 |
//     | 10 20 30 1 |  (column-major: m[3][0,1,2] = translation)
```

**Scaling:**
```c
SdkMat4 m;
sdk_mat4_scaling(&m, sdk_vec3(2, 2, 2));
// m = | 2 0 0 0 |
//     | 0 2 0 0 |
//     | 0 0 2 0 |
//     | 0 0 0 1 |
```

**Rotation Matrices:**

```c
// X-axis rotation (pitch)
SdkMat4 rx;
sdk_mat4_rotation_x(&rx, SDK_DEG2RAD * 45.0f);  // 45 degrees
// | 1  0   0  0 |
// | 0  c   s  0 |  c = cos(angle), s = sin(angle)
// | 0 -s   c  0 |
// | 0  0   0  1 |

// Y-axis rotation (yaw)
SdkMat4 ry;
sdk_mat4_rotation_y(&ry, SDK_DEG2RAD * 90.0f);
// |  c  0 -s  0 |
// |  0  1  0  0 |
// |  s  0  c  0 |
// |  0  0  0  1 |

// Z-axis rotation (roll)
SdkMat4 rz;
sdk_mat4_rotation_z(&rz, SDK_DEG2RAD * 30.0f);
// | c  s  0  0 |
// |-s  c  0  0 |
// | 0  0  1  0 |
// | 0  0  0  1 |
```

**Perspective Projection:**
```c
SdkMat4 proj;
sdk_mat4_perspective(&proj, 60.0f, 16.0f/9.0f, 0.1f, 1000.0f);
// fov_deg: Vertical field of view in degrees
// aspect:  Width/height ratio
// near:    Near clipping plane (must be > 0)
// far:     Far clipping plane

// Result (left-handed):
// | f/aspect  0      0           0 |
// | 0         f      0           0 |  f = 1/tan(fov/2)
// | 0         0      far/(far-near)  1 |
// | 0         0     -near*far/(far-near)  0 |
```

**Look-At View Matrix:**
```c
SdkVec3 eye = sdk_vec3(0, 5, -10);
SdkVec3 target = sdk_vec3(0, 0, 0);
SdkVec3 up = sdk_vec3(0, 1, 0);

SdkMat4 view;
sdk_mat4_look_at(&view, eye, target, up);
// Creates view matrix: transforms world to camera space
// Z axis points toward target (left-handed)
// X axis points right
// Y axis points up
```

---

## Common Transform Patterns

### Model-View-Projection Chain

```c
// World transform (model)
SdkMat4 world, rot, trans, scale;
sdk_mat4_rotation_y(&rot, SDK_DEG2RAD * angle);
sdk_mat4_translation(&trans, position);
sdk_mat4_scaling(&scale, sdk_vec3(1, 1, 1));
sdk_mat4_multiply(&world, &rot, &trans);

// View transform (camera)
SdkMat4 view;
sdk_mat4_look_at(&view, camera_pos, camera_target, camera_up);

// Projection
SdkMat4 proj;
sdk_mat4_perspective(&proj, fov, aspect, near_plane, far_plane);

// Combined: wvp = proj * view * world
SdkMat4 view_world, wvp;
sdk_mat4_multiply(&view_world, &view, &world);
sdk_mat4_multiply(&wvp, &proj, &view_world);

// Upload to shader
shader_set_matrix("WorldViewProj", &wvp);
```

### FPS Camera Setup

```c
typedef struct {
    SdkVec3 position;
    float pitch;  // X rotation
    float yaw;    // Y rotation
} FpsCamera;

void fps_camera_matrix(FpsCamera* cam, SdkMat4* view) {
    SdkMat4 rot_x, rot_y, trans;
    
    // Rotation order: Y then X (yaw then pitch)
    sdk_mat4_rotation_y(&rot_y, cam->yaw);
    sdk_mat4_rotation_x(&rot_x, cam->pitch);
    
    // Translation (negated for view matrix)
    sdk_mat4_translation(&trans, sdk_vec3(-cam->position.x, 
                                          -cam->position.y, 
                                          -cam->position.z));
    
    // Combine: rot_y * rot_x * trans
    SdkMat4 temp;
    sdk_mat4_multiply(&temp, &rot_y, &rot_x);
    sdk_mat4_multiply(view, &temp, &trans);
}
```

### Transform Point by Matrix

```c
SdkVec3 transform_point(const SdkMat4* m, SdkVec3 p) {
    // Column-major: multiply as column vector
    // result = M * p
    SdkVec3 r;
    r.x = m->m[0][0]*p.x + m->m[1][0]*p.y + m->m[2][0]*p.z + m->m[3][0];
    r.y = m->m[0][1]*p.x + m->m[1][1]*p.y + m->m[2][1]*p.z + m->m[3][1];
    r.z = m->m[0][2]*p.x + m->m[1][2]*p.y + m->m[2][2]*p.z + m->m[3][2];
    // Note: ignoring w component (assumes w=1, affine transform)
    return r;
}
```

### Direction Vectors from Yaw/Pitch

```c
SdkVec3 direction_from_angles(float yaw, float pitch) {
    float cy = cosf(yaw);
    float sy = sinf(yaw);
    float cp = cosf(pitch);
    float sp = sinf(pitch);
    
    // Forward direction (left-handed)
    return sdk_vec3(
        sy * cp,    // X
        sp,         // Y
        cy * cp     // Z (forward)
    );
}

void angles_from_direction(SdkVec3 dir, float* yaw, float* pitch) {
    *yaw = atan2f(dir.x, dir.z);   // atan2(X, Z) for left-handed
    *pitch = asinf(dir.y / sdk_vec3_length(dir));
}
```

---

## API Surface

### Public Header (sdk_math.h)

```c
/* Vector operations */
SdkVec3 sdk_vec3(float x, float y, float z);
SdkVec3 sdk_vec3_add(SdkVec3 a, SdkVec3 b);
SdkVec3 sdk_vec3_sub(SdkVec3 a, SdkVec3 b);
SdkVec3 sdk_vec3_scale(SdkVec3 v, float s);
float   sdk_vec3_dot(SdkVec3 a, SdkVec3 b);
SdkVec3 sdk_vec3_cross(SdkVec3 a, SdkVec3 b);
float   sdk_vec3_length(SdkVec3 v);
SdkVec3 sdk_vec3_normalize(SdkVec3 v);

/* Matrix operations (column-major for HLSL) */
void sdk_mat4_identity(SdkMat4* m);
void sdk_mat4_multiply(SdkMat4* out, const SdkMat4* a, const SdkMat4* b);
void sdk_mat4_translation(SdkMat4* m, SdkVec3 t);
void sdk_mat4_scaling(SdkMat4* m, SdkVec3 s);
void sdk_mat4_rotation_x(SdkMat4* m, float angle_rad);
void sdk_mat4_rotation_y(SdkMat4* m, float angle_rad);
void sdk_mat4_rotation_z(SdkMat4* m, float angle_rad);
void sdk_mat4_perspective(SdkMat4* m, float fov_deg, float aspect, float near_plane, float far_plane);
void sdk_mat4_look_at(SdkMat4* m, SdkVec3 eye, SdkVec3 target, SdkVec3 up);
```

---

## Integration Notes

### HLSL Shader Compatibility

The Math module uses column-major matrices compatible with HLSL:

```hlsl
// HLSL matrix (column-major by default)
float4x4 worldViewProj;

// C code uploads column-major data
sdk_mat4 wvp;
// ... compute wvp ...
shader_set_constant_buffer(&wvp, sizeof(SdkMat4));

// HLSL usage:
float4 pos = mul(float4(input.pos, 1.0), worldViewProj);
```

### OpenGL/GLSL Considerations

GLSL uses column-major by default (same as this module), but matrix multiplication order differs:

```glsl
// GLSL (same matrix layout)
vec4 pos = worldViewProj * vec4(input_pos, 1.0);  // Note: matrix first
```

### Row-Major Conversion

If you need row-major (e.g., for some APIs):

```c
void mat4_transpose(const SdkMat4* in, SdkMat4* out) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            out->m[i][j] = in->m[j][i];
}
```

### SIMD Optimization

Current implementation is scalar for portability. For production:
- Consider SIMD intrinsics (SSE/AVX) for batch operations
- Use DirectXMath or GLM if external dependencies acceptable
- Profile before optimizing - may be memory-bound

---

## AI Context Hints

### Creating Transform Hierarchies

```c
typedef struct Transform {
    SdkVec3 position;
    SdkVec3 rotation;  // Euler angles
    SdkVec3 scale;
    struct Transform* parent;
} Transform;

void transform_matrix(Transform* t, SdkMat4* out) {
    SdkMat4 trans, rot_x, rot_y, rot_z, scale, temp, rot;
    
    // Local transform
    sdk_mat4_translation(&trans, t->position);
    sdk_mat4_rotation_x(&rot_x, t->rotation.x);
    sdk_mat4_rotation_y(&rot_y, t->rotation.y);
    sdk_mat4_rotation_z(&rot_z, t->rotation.z);
    sdk_mat4_scaling(&scale, t->scale);
    
    // Combine rotations: Z * Y * X
    sdk_mat4_multiply(&temp, &rot_z, &rot_y);
    sdk_mat4_multiply(&rot, &temp, &rot_x);
    
    // S * R * T
    sdk_mat4_multiply(&temp, &scale, &rot);
    sdk_mat4_multiply(out, &temp, &trans);
    
    // Apply parent if exists
    if (t->parent) {
        SdkMat4 parent_mat;
        transform_matrix(t->parent, &parent_mat);
        
        SdkMat4 combined;
        sdk_mat4_multiply(&combined, &parent_mat, out);
        *out = combined;
    }
}
```

### Ray-Plane Intersection

```c
bool ray_plane_intersect(SdkVec3 ray_origin, SdkVec3 ray_dir,
                         SdkVec3 plane_point, SdkVec3 plane_normal,
                         float* t) {
    float denom = sdk_vec3_dot(plane_normal, ray_dir);
    if (fabsf(denom) < 0.0001f) return false;  // Parallel
    
    SdkVec3 diff = sdk_vec3_sub(plane_point, ray_origin);
    *t = sdk_vec3_dot(diff, plane_normal) / denom;
    return *t >= 0;
}
```

### Billboard Matrix (Sprite facing camera)

```c
void billboard_matrix(SdkVec3 sprite_pos, SdkMat4* view, SdkMat4* out) {
    // Extract camera up and right from view matrix
    SdkVec3 up = sdk_vec3(view->m[1][0], view->m[1][1], view->m[1][2]);
    SdkVec3 right = sdk_vec3(view->m[0][0], view->m[0][1], view->m[0][2]);
    
    // Build billboard matrix
    out->m[0][0] = right.x; out->m[0][1] = right.y; out->m[0][2] = right.z; out->m[0][3] = 0;
    out->m[1][0] = up.x;    out->m[1][1] = up.y;    out->m[1][2] = up.z;    out->m[1][3] = 0;
    out->m[2][0] = 0;       out->m[2][1] = 0;       out->m[2][2] = 1;       out->m[2][3] = 0;
    out->m[3][0] = sprite_pos.x;
    out->m[3][1] = sprite_pos.y;
    out->m[3][2] = sprite_pos.z;
    out->m[3][3] = 1;
}
```

### Orbit Camera

```c
typedef struct {
    float distance;
    float yaw;     // Rotation around Y
    float pitch;   // Rotation around X
    SdkVec3 target;
} OrbitCamera;

void orbit_camera_matrix(OrbitCamera* cam, SdkMat4* view) {
    // Convert spherical to cartesian
    float cy = cosf(cam->yaw);
    float sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch);
    float sp = sinf(cam->pitch);
    
    SdkVec3 eye;
    eye.x = cam->target.x + cam->distance * sy * cp;
    eye.y = cam->target.y + cam->distance * sp;
    eye.z = cam->target.z + cam->distance * cy * cp;
    
    SdkVec3 up = sdk_vec3(0, 1, 0);
    sdk_mat4_look_at(view, eye, cam->target, up);
}
```

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Related Systems
- [../Camera/SDK_Camera.md](../Camera/SDK_Camera.md) - Camera system (uses math)
- [../../Renderer/SDK_RendererRuntime.md](../../Renderer/SDK_RendererRuntime.md) - Rendering (uses transforms)
- [../Entities/SDK_Entities.md](../Entities/SDK_Entities.md) - Entity transforms
- [../API/Session/SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md) - Player position/rotation

---

**Source Files:**
- `SDK/Core/Math/sdk_math.h` (2,231 bytes) - Public API
- `SDK/Core/Math/sdk_math.c` (4,553 bytes) - Implementation

**External References:**
- DirectXMath: https://github.com/microsoft/DirectXMath
- GLM: https://github.com/g-truc/glm

---
*Documentation for `SDK/Core/Math/`*
