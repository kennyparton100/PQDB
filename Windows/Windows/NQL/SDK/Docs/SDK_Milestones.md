# NQL SDK — Milestone Roadmap

Phased development plan for the NQL rendering SDK. ✅ = Completed.

---

## M1: Triangle on Screen ✅

**Goal**: Window + single coloured triangle rendered with D3D12.

| Deliverable | Status |
|-------------|--------|
| Win32 window (800×600, resizable) | ✅ |
| D3D12 device, swap chain, double-buffered | ✅ |
| HLSL vertex + pixel shaders | ✅ |
| Hardcoded RGB triangle vertex buffer | ✅ |
| Frame loop with vsync + fence sync | ✅ |
| Resize handling (swap chain recreate) | ✅ |
| Public C API (`nqlsdk_init/frame/shutdown`) | ✅ |
| VS 2022 vcxproj (x64 Debug/Release) | ✅ |
| Documentation (overview, build, API, milestones) | ✅ |

---

## M2: Voxel World ✅

**Goal**: Procedural terrain, chunks, block interaction.

| Deliverable | Status |
|-------------|--------|
| Chunk data structure (16×128×16 blocks) | ✅ |
| Greedy meshing for world geometry | ✅ |
| Procedural terrain generation | ✅ |
| Block types: grass, dirt, stone, wood, leaves | ✅ |
| Block breaking (LMB) and placing (RMB) | ✅ |
| Hotbar system with item selection | ✅ |
| Tool speed bonuses (wood/stone vs hand) | ✅ |
| First-person camera with collision | ✅ |
| AABB physics and gravity | ✅ |
| Fall damage | ✅ |

---

## M3: Survival Systems ✅

**Goal**: Hunger, health, day/night cycle, death.

| Deliverable | Status |
|-------------|--------|
| Health system (20 HP, 10 hearts display) | ✅ |
| Hunger system (20 points, depletes over time) | ✅ |
| Starvation damage when hunger = 0 | ✅ |
| Natural healing when hunger ≥ 18 | ✅ |
| Sprinting (double-tap W, 1.5× speed) | ✅ |
| Sprint drains hunger 4× faster | ✅ |
| Day/night cycle (24000 tick days) | ✅ |
| Ambient lighting changes | ✅ |
| Death screen + respawn | ✅ |
| Clear inventory on death | ✅ |

---

## M4: Crafting System ✅

**Goal**: Recipe-based item creation.

| Deliverable | Status |
|-------------|--------|
| Crafting recipes data structure | ✅ |
| Recipe matching algorithm | ✅ |
| 2×2 hand crafting (C key) | ✅ |
| 3×3 crafting table | ✅ |
| Placeable crafting table block | ✅ |
| Recipes: planks, sticks, crafting table, stone tools | ✅ |
| Mouse-based crafting: left-click crafts 1 | ✅ |
| Mouse-based crafting: right-click crafts max | ✅ |
| Correct ingredient consumption | ✅ |
| Results go directly to hotbar | ✅ |

---

## M5: Entities ✅

**Goal**: Mobs, item drops, AI.

| Deliverable | Status |
|-------------|--------|
| Entity system architecture | ✅ |
| Item drops from broken blocks | ✅ |
| Mob spawning system | ✅ |
| Zombies spawn at night | ✅ |
| Zombie AI: chase player | ✅ |
| Mob damage on contact | ✅ |
| Mob drops (raw meat from zombies) | ✅ |
| Food items with nutrition values | ✅ |
| Cooked meat (furnace not yet implemented) | ✅ |

---

## M6: Renderer Polish ⏳

**Goal**: Visual improvements, performance.

- Frustum culling
- Texture atlasing for blocks
- Transparent block rendering (leaves, water)
- Better lighting model
- Shadows
- Particle effects

---

## M7: NQL Integration ⏳

**Goal**: NQL scripts can drive the renderer.

- Register SDK functions as NQL built-ins
- NQL can query world state
- NQL can create/modify geometry
- Frame loop callable from NQL imperative scripting
