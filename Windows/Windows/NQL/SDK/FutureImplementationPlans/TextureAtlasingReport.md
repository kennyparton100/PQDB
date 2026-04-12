# Texture Atlasing Implementation Report

## What Is Texture Atlasing

Texture atlasing is a graphics optimization technique where multiple small textures are combined into a single larger texture image (the "atlas"). Instead of loading and binding dozens or hundreds of individual texture files, the engine loads one or a few large textures containing all the smaller images arranged in a grid.

### Current SDK Approach
The SDK currently uses a `Texture2DArray` approach where each block texture is a separate slice in an array texture. This already provides some benefits over traditional individual texture binding, as the shader can index into the array without changing GPU state.

### Atlas Approach
With texture atlasing, all block textures would be packed into one or more large 2D textures (e.g., 4096x4096 or 8192x8192). Each block's UV coordinates would be adjusted to sample from the correct region of the atlas.

## Performance Benefits

### For the SDK Specifically

Given the SDK already uses `Texture2DArray`, the benefits of switching to atlasing are **modest** rather than dramatic:

**Estimated Gains:**
- **5-15% overall frame time improvement** in texture-bound scenarios
- **Reduced memory overhead** from fewer texture headers and padding
- **Faster texture loading** at startup (one large load vs many small loads)
- **Better GPU cache locality** when drawing many blocks from the same atlas

**Why Gains Are Moderate:**
- `Texture2DArray` already avoids expensive texture state changes
- The SDK's chunk-based rendering already batches geometry efficiently
- Most rendering cost is in vertex processing and mesh generation, not texture sampling

### Where Atlasing Would Shine
- **Mobile or low-end GPUs** with limited texture binding capabilities
- **Scenes with thousands of unique textured objects** (less relevant for block-based worlds)
- **Memory-constrained environments** where texture header overhead matters

## Implementation Approach

### Phase 1: Atlas Generation (Build-Time or Runtime)

**Option A: Build-Time Tool (Recommended)**
- Create a standalone tool that scans `TexturePacks/Default/blocks/`
- Packs all PNG files into one or more atlas textures using rectangle packing algorithms
- Outputs:
  - Atlas texture file(s) (PNG or DDS)
  - Metadata file mapping block IDs → UV coordinates/regions
- Run as part of build process or when texture pack changes

**Option B: Runtime Generation**
- Load all individual textures at startup
- Pack them into atlas textures in-memory
- More flexible but adds startup overhead
- Need to handle atlas regeneration when texture pack changes

### Phase 2: Renderer Integration

**Current Code (Texture2DArray):**
```cpp
// Shader uses array index
Texture2DArray blockTextures : register(t0);
uint textureIndex = blockType * 6 + face;
```

**New Code (Atlas):**
```cpp
// Shader uses UV coordinates
Texture2D blockAtlas : register(t0);
SamplerState atlasSampler : register(s0);
// UVs adjusted per-vertex to sample correct atlas region
```

**Changes Required:**
1. Replace `Texture2DArray` with `Texture2D` in shaders
2. Add UV offset/scaling to vertex data or use a uniform buffer with atlas regions
3. Update mesh builder to apply UV transforms per block type
4. Modify texture loading to read atlas + metadata instead of individual files

### Phase 3: Multi-Atlas Support

If texture count exceeds single atlas capacity:
- Implement atlas selection logic (block type → which atlas)
- Either:
  - Use multiple texture binds (draw calls split by atlas)
  - Use texture array of atlases (hybrid approach)

## Development Risks

### High Risk

**1. UV Coordinate Precision**
- Large atlases (8192x8192+) require careful UV handling
- Floating-point precision issues at edges can cause bleeding between textures
- **Mitigation**: Add padding/gutter between atlas regions, use half-float UVs if needed

**2. Mipmap Generation**
- Atlas mipmaps can cause bleeding if not generated carefully
- Neighboring textures in atlas can bleed into each other at lower mipmap levels
- **Mitigation**: Use specialized mipmap generation with padding, or disable mipmaps for atlas

**3. Texture Filtering Artifacts**
- Linear filtering can sample across atlas region boundaries
- Visible seams or color bleeding at block edges
- **Mitigation**: Conservative UV clamping, border pixels, or nearest filtering for blocks

### Medium Risk

**4. Atlas Packing Complexity**
- Efficient rectangle packing is non-trivial
- Need to handle varying texture sizes (though SDK uses fixed 16x16)
- **Mitigation**: Use established packing libraries (e.g., stb_rect_pack)

**5. Dynamic Texture Updates**
- If textures can change at runtime (hot-reloading), need atlas regeneration
- Atlas regeneration is expensive
- **Mitigation**: Treat atlas as immutable after load, or use sparse atlas updates

**6. Shader Complexity**
- UV math becomes more complex (offsets + scaling per block)
- May need uniform buffer updates or more vertex data
- **Mitigation**: Bake UV transforms into vertex data during mesh building

### Low Risk

**7. Backward Compatibility**
- Need to support both Texture2DArray and Atlas approaches during transition
- **Mitigation**: Feature flag or graphics setting to switch between modes

**8. Tooling Overhead**
- Build-time atlas tool adds to development workflow
- **Mitigation**: Automate in build script, cache atlas when source files unchanged

## Recommendation

**Do not prioritize texture atlasing for the SDK at this time.**

**Reasons:**
1. **Modest ROI**: 5-15% gain for significant implementation effort
2. **Existing solution works**: `Texture2DArray` already solves the main problem (state changes)
3. **High risk**: UV bleeding, mipmap artifacts, and precision issues are tricky
4. **Better targets available**: Other M6 features (better LOD, shadow improvements) likely offer better ROI

**When to reconsider:**
- Profiling shows texture sampling is a bottleneck (unlikely given current architecture)
- Targeting low-end mobile devices where texture binding is expensive
- Need to reduce memory footprint significantly

**If proceeding anyway:**
- Start with build-time atlas tool (cleaner than runtime)
- Use generous padding between atlas regions (2-4 pixels)
- Test extensively for UV bleeding artifacts
- Keep Texture2DArray path as fallback for compatibility
