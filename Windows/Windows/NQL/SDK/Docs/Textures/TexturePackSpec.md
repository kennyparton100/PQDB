<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Textures

---

# Texture Pack Spec

The renderer now supports a Minecraft-style block texture pack path.

## Pack Root

Preferred runtime location:

```text
<exe folder>/TexturePacks/Default/blocks/
```

Development fallback location:

```text
Windows/NQL/SDK/TexturePacks/Default/blocks/
```

If a texture is missing, the game falls back to a generated flat-color tile from the current block face color, so incomplete packs still work.

## Image Format

- File type: `PNG`
- Pixel format: `RGBA`
- Tile size: `16x16`
- Shape: square only
- One file per block-face asset name
- Alpha is supported

V1 is intentionally fixed at `16x16` so the pack format is stable while the renderer and block set are still moving.

## Naming Rules

Most blocks use this default rule:

```text
blocks/<block_name>.png
```

That means these examples work directly:

```text
blocks/granite.png
blocks/gneiss.png
blocks/shale.png
blocks/limestone.png
blocks/dolostone.png
blocks/basalt.png
blocks/stone_bricks.png
blocks/tuff.png
blocks/scoria.png
blocks/topsoil.png
blocks/subsoil.png
blocks/peat.png
blocks/water.png
blocks/sea_ice.png
blocks/coal_seam.png
blocks/copper_bearing_rock.png
```

## Special Face-Specific Assets

These blocks use different filenames for top / bottom / side faces:

- `grass`
  - top: `grass_top.png`
  - bottom: `dirt.png`
  - sides: `grass_side.png`
- `turf`
  - top: `turf_top.png`
  - bottom: `dirt.png`
  - sides: `turf_side.png`
- `wetland_sod`
  - top: `wetland_sod_top.png`
  - bottom: `dirt.png`
  - sides: `wetland_sod_side.png`
- `log`
  - top and bottom: `log_top.png`
  - sides: `log_side.png`
- `crafting_table`
  - top: `crafting_table_top.png`
  - bottom: `planks.png`
  - sides: `crafting_table_side.png`
- `furnace`
  - top: `furnace_top.png`
  - bottom: `furnace_bottom.png`
  - sides: `furnace_side.png`
- `campfire`
  - top: `campfire_top.png`
  - bottom: `campfire_bottom.png`
  - sides: `campfire_side.png`
- `anvil`
  - top: `anvil_top.png`
  - bottom: `anvil_bottom.png`
  - sides: `anvil_side.png`
- `blacksmithing_table`
  - top: `blacksmithing_table_top.png`
  - bottom: `blacksmithing_table_bottom.png`
  - sides: `blacksmithing_table_side.png`
- `leatherworking_table`
  - top: `leatherworking_table_top.png`
  - bottom: `leatherworking_table_bottom.png`
  - sides: `leatherworking_table_side.png`

## Recommended First-Pass Coverage

If you want the fastest visual improvement, start with:

- `topsoil.png`
- `subsoil.png`
- `turf_top.png`
- `turf_side.png`
- `wetland_sod_top.png`
- `wetland_sod_side.png`
- `sand.png`
- `silt.png`
- `clay.png`
- `coarse_alluvium.png`
- `fine_alluvium.png`
- `colluvium.png`
- `talus.png`
- `peat.png`
- `granite.png`
- `gneiss.png`
- `schist.png`
- `basalt.png`
- `volcanic_rock.png`
- `sandstone.png`
- `shale.png`
- `limestone.png`
- `dolostone.png`
- `mudstone.png`
- `siltstone.png`
- `conglomerate.png`
- `marl.png`
- `chalk.png`
- `andesite.png`
- `tuff.png`
- `scoria.png`
- `water.png`
- `ice.png`
- `sea_ice.png`

## Art Guidance

- Make tiles seamlessly tiling on all four edges.
- Keep geology textures lower-contrast than Minecraft if the goal is realism.
- Use directional bedding sparingly; if everything has strong stripes the underground will still read as procedural.
- Save strong linear structure for the blocks that should really show it, such as some sedimentary units, foliated metamorphics, and volcanic units.
- Use alpha mainly for vegetation-like textures such as `leaves`, `reeds`, and `berry_bush`.

## Current Runtime Behavior

- Chunk meshes reference textures by `block * 6 + face` in a `Texture2DArray`.
- HUD, outlines, and debug-color overlays stay untextured.
- Missing PNGs do not break rendering; they use generated fallback tiles.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Related Systems
- [../Renderer/SDK_RendererRuntime.md](../Renderer/SDK_RendererRuntime.md) - Rendering system
- [../Shaders/SDK_Shaders.md](../Shaders/SDK_Shaders.md) - Shader pipeline
- [../Core/World/Blocks/SDK_Blocks.md](../Core/World/Blocks/SDK_Blocks.md) - Block types (uses textures)
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build guide (asset copy)

---
*Documentation for `SDK/Textures/`*
