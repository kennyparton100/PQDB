<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Camera

---

# SDK Camera System

## Overview

The Camera system provides two complementary components for 3D rendering and gameplay:

1. **SdkCamera Object** - A standalone utility camera with view frustum culling for rendering, chunk visibility testing, and scene management
2. **Global Gameplay Camera** - Euler-angle based player view using yaw/pitch with look-at target calculation

**Files:** `SDK/Core/Camera/sdk_camera.c` (175 lines), `sdk_camera.h` (74 lines)  
**Public API:** `sdk_camera_init()`, `sdk_camera_update()`, `sdk_frustum_contains_*()`  
**Dependencies:** Math library (`SdkVec3`, `SdkMat4`), Input system, Renderer

## Architecture

### SdkCamera Structure

```c
typedef struct SdkCamera {
    // Transform
    SdkVec3 position;   // Eye position
    SdkVec3 target;     // Look-at point
    SdkVec3 up;         // World up (typically 0,1,0)
    
    // Projection parameters
    float fov_deg;      // Field of view (60° default)
    float aspect;       // Width/height ratio
    float near_plane;   // Near clip (0.1M default)
    float far_plane;    // Far clip (100M default)
    
    // Cached matrices (updated by sdk_camera_update)
    SdkMat4 view;           // World → View transform
    SdkMat4 projection;     // View → Clip transform
    SdkMat4 view_projection; // Combined MVP matrix
    
    // Cached frustum (updated by sdk_camera_update)
    SdkFrustum frustum;   // 6 planes for culling
} SdkCamera;
```

### View Frustum

```c
typedef struct SdkFrustum {
    float planes[6][4];  // 6 planes: left, right, bottom, top, near, far
                         // Each: (nx, ny, nz, d) where nx*x + ny*y + nz*z + d = 0
} SdkFrustum;
```

**Plane Order:**
0. Left
1. Right
2. Bottom
3. Top
4. Near
5. Far

### Data Flow

```
Application Setup
       │
       ├──► sdk_camera_init(cam, aspect)
       │
       ▼
Per-Frame Update Loop
       │
       ├──► sdk_camera_look_at(cam, position, target)
       ├──► sdk_camera_set_projection(cam, fov, aspect, near, far)
       └──► sdk_camera_update(cam)
              │
              ├──► sdk_mat4_look_at() → view matrix
              ├──► sdk_mat4_perspective() → projection matrix
              ├──► sdk_mat4_multiply() → view_projection
              └──► sdk_camera_extract_frustum() → 6 planes
                          │
                          ▼
            Plane Extraction (from view_projection):
            Left:   row3 + row0
            Right:  row3 - row0
            Bottom: row3 + row1
            Top:    row3 - row1
            Near:   row2 (D3D: z in [0,w])
            Far:    row3 - row2
                          │
                          ▼
              Normalize all 6 plane normals
                          │
                          ▼
       Culling Tests (rendering loop)
              │
              ├──► sdk_frustum_contains_sphere() → bool
              └──► sdk_frustum_contains_aabb() → bool
```

## Key Functions

### SdkCamera Lifecycle

**`void sdk_camera_init(SdkCamera* cam, float aspect)`**
- Sets position to origin (0,0,0)
- Sets target to (0,0,-1) — looking down -Z
- Sets up to (0,1,0) — +Y is up
- Default projection: 60° FOV, 0.1 near, 100 far
- Initializes all matrices to identity

**`void sdk_camera_update(SdkCamera* cam)`**
- Recomputes view matrix from position/target/up
- Recomputes projection from FOV/aspect/near/far
- Multiplies: view_projection = projection × view
- Extracts and normalizes frustum planes
- Call this after changing any camera parameters

**`void sdk_camera_look_at(SdkCamera* cam, SdkVec3 position, SdkVec3 target)`**
- Updates position and target (up unchanged)
- Does NOT update matrices — call `sdk_camera_update()` after

**`void sdk_camera_set_projection(SdkCamera* cam, float fov_deg, float aspect, float near_plane, float far_plane)`**
- Updates all projection parameters
- Does NOT update matrices — call `sdk_camera_update()` after

### Frustum Extraction

**`void sdk_camera_extract_frustum(SdkCamera* cam)`**

Derives 6 clipping planes from view_projection matrix using D3D12 conventions:

| Plane | Formula | Notes |
|-------|---------|-------|
| Left | row3 + row0 | X negative boundary |
| Right | row3 - row0 | X positive boundary |
| Bottom | row3 + row1 | Y negative boundary |
| Top | row3 - row1 | Y positive boundary |
| Near | row2 | Z=0 clip plane (D3D) |
| Far | row3 - row2 | Z=far clip plane |

All plane normals are normalized to unit length.

### Culling Tests

**`bool sdk_frustum_contains_sphere(const SdkFrustum* frustum, float cx, float cy, float cz, float radius)`**

Tests sphere against all 6 frustum planes:
- Computes signed distance from center to each plane
- Plane normals point INWARD (toward view volume)
- Sphere is OUTSIDE if `dist < -radius` for any plane
- Returns `true` if sphere intersects or is inside frustum

**`bool sdk_frustum_contains_aabb(const SdkFrustum* frustum, float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)`**

Tests axis-aligned bounding box against frustum:
- Computes box center and extents (half-sizes)
- For each plane, computes effective radius: `|nx|*ex + |ny|*ey + |nz|*ez`
- Box is OUTSIDE if `center_dist < -effective_radius` for any plane
- Returns `true` if AABB intersects or is inside frustum

## Global Gameplay Camera

The gameplay camera uses a separate Euler-angle system for player view control.

### State Variables

**Declaration:** `sdk_api_internal.h:555-558`
```c
extern float g_cam_yaw;                    // Horizontal rotation
extern float g_cam_pitch;                  // Vertical rotation
extern float g_cam_look_dist;              // Distance to look target
extern bool g_cam_rotation_initialized;    // First-frame flag
```

**Definition:** `sdk_api.c:26-29`
```c
float g_cam_yaw = 0.0f;
float g_cam_pitch = 0.0f;
float g_cam_look_dist = 1.0f;
bool g_cam_rotation_initialized = false;
```

**Defaults:** `sdk_api_internal.h:88-89`
```c
#define CAM_DEFAULT_YAW 0.0f
#define CAM_DEFAULT_PITCH -0.3f
```

### Look Target Calculation

From yaw/pitch to 3D look target:

```c
float cos_p = cosf(g_cam_pitch);
float sin_p = sinf(g_cam_pitch);

look_x = position_x + g_cam_look_dist * cos_p * sinf(g_cam_yaw);
look_y = position_y + g_cam_look_dist * sinf(g_cam_pitch);
look_z = position_z + g_cam_look_dist * cos_p * cosf(g_cam_yaw);
```

**Direction Vectors:**

| Vector | X | Y | Z |
|--------|---|---|---|
| Forward (look) | cos(pitch)·sin(yaw) | sin(pitch) | cos(pitch)·cos(yaw) |
| Right (strafe) | cos(yaw) | 0 | -sin(yaw) |

### Input Handling

**Arrow Key Camera Control** (`sdk_api.c`):

```c
float yaw = g_cam_yaw;
float pitch = g_cam_pitch;
float rot_speed = 0.03f * (g_input_settings.look_sensitivity_percent / 100.0f);

if (LOOK_LEFT)  yaw -= rot_speed;
if (LOOK_RIGHT) yaw += rot_speed;
if (LOOK_UP)    pitch += rot_speed * (invert_y ? -1 : 1);
if (LOOK_DOWN)  pitch -= rot_speed * (invert_y ? -1 : 1);

// Clamp pitch to prevent gimbal lock
if (pitch >  1.5f) pitch =  1.5f;
if (pitch < -1.5f) pitch = -1.5f;

g_cam_yaw = yaw;
g_cam_pitch = pitch;
```

### Integration Points

**Renderer Integration:**
```c
// Each frame, compute look target and update renderer
sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
sdk_renderer_set_camera_target(look_x, look_y, look_z);
```

**Vehicle Mounting:**
- `sync_camera_to_vehicle()` - Camera locked to vehicle position
- Preserves yaw/pitch but position follows vehicle entity

**Teleport/Session Restore:**
- `teleport_player_to()` - Updates position, preserves yaw/pitch
- Session load: Restores `g_cam_yaw`/`g_cam_pitch` from `SdkPersistedState`

**Initialization Points:**
- First frame: `if (!g_cam_rotation_initialized) { yaw = CAM_DEFAULT_YAW; pitch = CAM_DEFAULT_PITCH; }`
- Session reset: `g_cam_yaw = CAM_DEFAULT_YAW; g_cam_pitch = CAM_DEFAULT_PITCH;`
- Editor modes: Fixed angles (yaw=0.0, pitch=-0.28 or -0.44)

## API Surface Summary

### SdkCamera Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `sdk_camera_init` | `cam*, aspect` | - | Default initialization |
| `sdk_camera_update` | `cam*` | Updated matrices/frustum | Recompute from current state |
| `sdk_camera_look_at` | `cam*, pos, target` | - | Set transform (needs update) |
| `sdk_camera_set_projection` | `cam*, fov, aspect, near, far` | - | Set projection (needs update) |
| `sdk_camera_extract_frustum` | `cam*` | Updated `cam->frustum` | Derive planes from matrices |
| `sdk_frustum_contains_sphere` | `frustum*, cx, cy, cz, r` | `bool` | Sphere visibility test |
| `sdk_frustum_contains_aabb` | `frustum*, min, max` | `bool` | Box visibility test |

### Global Camera Access

| Variable | Type | Default | Valid Range | Usage |
|----------|------|---------|-------------|-------|
| `g_cam_yaw` | `float` | 0.0f | [0, 2π] | Horizontal angle |
| `g_cam_pitch` | `float` | -0.3f | [-1.5, 1.5] | Vertical angle |
| `g_cam_look_dist` | `float` | 1.0f | > 0 | Look target distance |
| `g_cam_rotation_initialized` | `bool` | false | true/false | First-frame flag |

## Global State Dependencies

### SdkCamera Object

**Owned by:** Consuming modules (Scene, Renderer, Debugger)  
**Not global** — each module creates its own `SdkCamera` instance as needed.

### Gameplay Camera

**Cross-module dependencies:**

```c
// Input system
extern SdkInputSettings g_input_settings;     // For sensitivity/invert_y

// Renderer
void sdk_renderer_set_camera_pos(float x, float y, float z);
void sdk_renderer_set_camera_target(float x, float y, float z);

// Vehicle system
SdkEntity* mounted_vehicle_entity();
float sdk_entity_vehicle_eye_height(MobType type);

// Session persistence
typedef struct {
    float cam_yaw;
    float cam_pitch;
    // ... other fields
} SdkPersistedState;
```

## AI Context Hints

### Coordinate System

- **Left-handed**: +X right, +Y up, +Z forward (into screen)
- **Yaw 0**: Facing -Z (standard "forward" in game)
- **Yaw +X**: Rotating right (clockwise from above)
- **Pitch positive**: Looking up (toward +Y)
- **Pitch negative**: Looking down (toward -Y)

### D3D12 Conventions

- **Column-vector math**: `pos' = MVP × pos` (matrix on left)
- **Matrix layout**: `m[col][row]` — column-major storage
- **Clip space**: z in [0, w] (not [-w, w] like OpenGL)
- **Multiplication order**: `MVP = P × V` (projection then view)

### Common Operations

**Get forward direction vector:**
```c
float cos_p = cosf(g_cam_pitch);
float sin_p = sinf(g_cam_pitch);
float sin_y = sinf(g_cam_yaw);
float cos_y = cosf(g_cam_yaw);

float forward_x = cos_p * sin_y;
float forward_y = sin_p;
float forward_z = cos_p * cos_y;
```

**Get right vector (for strafing):**
```c
float right_x = cosf(g_cam_yaw);
float right_z = -sinf(g_cam_yaw);
```

**Move camera forward by distance:**
```c
position.x += forward_x * distance;
position.y += forward_y * distance;
position.z += forward_z * distance;
```

### Frustum Culling Notes

- **Plane normals point INWARD** toward the view volume
- **Positive distance** = point is on the inside side of plane
- **Negative distance** = point is outside
- **Effective radius for AABB**: Project extent onto plane normal

### Performance Considerations

- `sdk_camera_update()` recomputes ALL matrices and frustum — call once per frame
- `sdk_frustum_contains_*` tests are cheap — safe to call for many objects
- Consider frustum culling before expensive render operations

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Related Systems
- [../Math/SDK_Math.md](../Math/SDK_Math.md) - Vector/matrix math functions
- [../../Renderer/SDK_RendererRuntime.md](../../Renderer/SDK_RendererRuntime.md) - Rendering and camera setup
- [../Scene/SDK_Scene.md](../Scene/SDK_Scene.md) - Scene management
- [../World/Chunks/ChunkManager/SDK_ChunkManager.md](../World/Chunks/ChunkManager/SDK_ChunkManager.md) - Chunk visibility using frustum culling
- [../Input/SDK_Input.md](../Input/SDK_Input.md) - Input (camera controls)

---
*Documentation for `SDK/Core/Camera/`*
