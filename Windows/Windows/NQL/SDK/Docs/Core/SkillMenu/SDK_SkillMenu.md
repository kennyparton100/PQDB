# SDK SkillMenu Documentation

Comprehensive documentation for the SDK SkillMenu module providing the in-game skills and professions menu with point allocation and progression tracking.

**Module:** `SDK/Core/SkillMenu/`  
**Output:** `SDK/Docs/Core/SkillMenu/SDK_SkillMenu.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Skill Tabs](#skill-tabs)
- [Skill Points System](#skill-points-system)
- [Progression Formulas](#progression-formulas)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The SkillMenu module implements the player skills interface with three main tabs: Combat skills, Survival skills, and Professions. Players earn skill points through leveling and can allocate them to improve character capabilities.

**Key Features:**
- Three skill tabs (Combat, Survival, Professions)
- General skill points from player leveling
- Profession-specific points from profession XP
- Maximum rank caps per skill
- Keyboard/gamepad navigation
- Integration with pause menu system

---

## Architecture

### Menu Flow

```
Player presses Skills key (Tab):
           │
           ├──► g_skills_open = true
           ├──► skills_handle_menu_input() processes each frame
           │       ├──► Navigation (arrows, Q/E for professions)
           │       ├──► Confirm (Enter) to spend points
           │       └──► Back (Escape) to close
           │
           └──► UI renders skill menu overlay
```

### Point Allocation Flow

```
Player presses Enter on skill:
           │
           ├──► Check available points:
           │       ├──► Combat/Survival: available_general_skill_points()
           │       └──► Professions: g_profession_points[selected_prof]
           │
           ├──► Check current rank < max rank:
           │       ├──► Combat/Survival: GENERAL_SKILL_MAX_RANK
           │       └──► Professions: PROFESSION_SKILL_MAX_RANK
           │
           ├──► Increment rank:
           │       ├──► g_combat_skill_ranks[row]++
           │       └──► Decrement available points
           │
           └──► Apply skill effects (handled by gameplay systems)
```

---

## Skill Tabs

### Tab Structure

| Tab | Constant | Row Count | Content |
|-----|----------|-----------|---------|
| Combat | `SDK_SKILL_TAB_COMBAT` | `SDK_COMBAT_SKILL_COUNT` | Combat abilities |
| Survival | `SDK_SKILL_TAB_SURVIVAL` | `SDK_SURVIVAL_SKILL_COUNT` | Survival abilities |
| Professions | `SDK_SKILL_TAB_PROFESSIONS` | `SDK_PROFESSION_NODE_COUNT` | Profession skills |

### Navigation

```c
// Tab switching (left/right arrows)
if (nav_left && g_skills_selected_tab > 0) {
    g_skills_selected_tab--;
    g_skills_selected_row = 0;
}
if (nav_right && g_skills_selected_tab < SDK_SKILL_TAB_COUNT - 1) {
    g_skills_selected_tab++;
    g_skills_selected_row = 0;
}

// Row selection (up/down arrows)
if (nav_up && g_skills_selected_row > 0) {
    g_skills_selected_row--;
}
if (nav_down && g_skills_selected_row < row_count - 1) {
    g_skills_selected_row++;
}

// Profession switching (Q/E keys) - only in Professions tab
if (nav_q && g_skills_selected_profession > 0) {
    g_skills_selected_profession--;
}
if (nav_e && g_skills_selected_profession < SDK_PROFESSION_COUNT - 1) {
    g_skills_selected_profession++;
}
```

---

## Skill Points System

### General Skill Points

**Formula:** Points = triangular number of level

```c
int total_skill_points_for_level(int level) {
    if (level <= 0) return 0;
    return (level * (level + 1)) / 2;  // 1, 3, 6, 10, 15, 21...
}

// Example progression:
// Level 1: 1 point
// Level 2: 3 points (+2)
// Level 3: 6 points (+3)
// Level 4: 10 points (+4)
// Level 5: 15 points (+5)
```

**Spent Points Calculation:**

```c
int spent_general_skill_points() {
    int spent = 0;
    for (i = 0; i < SDK_COMBAT_SKILL_COUNT; i++) 
        spent += g_combat_skill_ranks[i];
    for (i = 0; i < SDK_SURVIVAL_SKILL_COUNT; i++) 
        spent += g_survival_skill_ranks[i];
    return spent;
}

int available_general_skill_points() {
    int available = total_skill_points_for_level(g_player_level) 
                  - spent_general_skill_points();
    return available > 0 ? available : 0;
}
```

### Profession Points

Profession points are tracked separately per profession:

```c
// Per-profession state
int g_profession_points[SDK_PROFESSION_COUNT];   // Available points
int g_profession_levels[SDK_PROFESSION_COUNT];     // Profession level
int g_profession_xp[SDK_PROFESSION_COUNT];           // Current XP
int g_profession_xp_to_next[SDK_PROFESSION_COUNT]; // XP needed for next level
```

**Professions:**
- `SDK_PROFESSION_MINING` - Mining and resource extraction
- `SDK_PROFESSION_SMITHING` - Metal crafting
- `SDK_PROFESSION_WOODCUTTING` - Wood gathering
- `SDK_PROFESSION_FARMING` - Agriculture
- ... (others defined in profession system)

---

## Progression Formulas

### Player Leveling

| Variable | Purpose | Initial Value |
|----------|---------|---------------|
| `g_player_level` | Current character level | 1 |
| `g_player_xp` | Current XP | 0 |
| `g_player_xp_to_next` | XP to next level | 100 |

### Skill Ranks

| Skill Type | Max Rank Constant | Typical Value |
|------------|-------------------|---------------|
| Combat | `GENERAL_SKILL_MAX_RANK` | ~10 |
| Survival | `GENERAL_SKILL_MAX_RANK` | ~10 |
| Profession | `PROFESSION_SKILL_MAX_RANK` | ~20 |

### Rank Arrays

```c
// Combat and Survival ranks (indexed by row in menu)
uint8_t g_combat_skill_ranks[SDK_COMBAT_SKILL_COUNT];
uint8_t g_survival_skill_ranks[SDK_SURVIVAL_SKILL_COUNT];

// Profession ranks (indexed by profession, then node)
uint8_t g_profession_ranks[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT];
```

---

## Key Functions

### Point Calculations

| Function | Signature | Description |
|----------|-----------|-------------|
| `total_skill_points_for_level` | `(int level) → int` | Total points earned at level |
| `spent_general_skill_points` | `() → int` | Points spent on combat/survival |
| `available_general_skill_points` | `() → int` | Unspent general points |

### Menu Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `skills_row_count_for_tab` | `(int tab) → int` | Get row count for tab |
| `skills_clamp_selection` | `() → void` | Clamp selection to valid ranges |
| `skills_reset_progression` | `() → void` | Reset all skills/progression |
| `skills_handle_menu_input` | `() → void` | Process frame input |

---

## Global State

### Menu State

```c
// Menu open state
extern bool g_skills_open;
extern bool g_skills_key_was_down;
extern int  g_skills_key_frames;

// Navigation state
extern int g_skills_selected_tab;        // SDK_SKILL_TAB_COMBAT, etc.
extern int g_skills_selected_row;        // Selected skill row
extern int g_skills_selected_profession; // Selected profession
extern bool g_skills_nav_was_down[8];    // Previous frame key states
```

### Player Progression

```c
// Core progression
extern int g_player_level;
extern int g_player_xp;
extern int g_player_xp_to_next;

// General skills
extern uint8_t g_combat_skill_ranks[SDK_COMBAT_SKILL_COUNT];
extern uint8_t g_survival_skill_ranks[SDK_SURVIVAL_SKILL_COUNT];

// Professions
extern int g_profession_points[SDK_PROFESSION_COUNT];
extern int g_profession_levels[SDK_PROFESSION_COUNT];
extern int g_profession_xp[SDK_PROFESSION_COUNT];
extern int g_profession_xp_to_next[SDK_PROFESSION_COUNT];
extern uint8_t g_profession_ranks[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT];
```

### Related Pause Menu State

The skills menu shares state with the pause menu system:

```c
// Pause menu (managed alongside skills)
extern bool g_pause_menu_open;
extern bool g_pause_menu_key_was_down;
extern int  g_pause_menu_view;
extern int  g_pause_menu_selected;
```

---

## API Surface

### Public Interface (sdk_skills_menu.c functions)

```c
/* Skill point calculations */
int total_skill_points_for_level(int level);
int spent_general_skill_points(void);
int available_general_skill_points(void);

/* Menu state management */
int skills_row_count_for_tab(int tab);
void skills_clamp_selection(void);
void skills_reset_progression(void);

/* Input handling - called each frame when skills menu open */
void skills_handle_menu_input(void);
```

### Constants

```c
// Tab indices
typedef enum {
    SDK_SKILL_TAB_COMBAT = 0,
    SDK_SKILL_TAB_SURVIVAL,
    SDK_SKILL_TAB_PROFESSIONS,
    SDK_SKILL_TAB_COUNT
} SdkSkillTab;

// Profession indices
typedef enum {
    SDK_PROFESSION_MINING = 0,
    SDK_PROFESSION_SMITHING,
    SDK_PROFESSION_WOODCUTTING,
    SDK_PROFESSION_FARMING,
    // ... etc
    SDK_PROFESSION_COUNT
} SdkProfession;

// Limits (example values)
#define SDK_COMBAT_SKILL_COUNT     8
#define SDK_SURVIVAL_SKILL_COUNT   8
#define SDK_PROFESSION_NODE_COUNT  10
#define GENERAL_SKILL_MAX_RANK     10
#define PROFESSION_SKILL_MAX_RANK  20
```

---

## Integration Notes

### Input Integration

```c
// In main input handling
void handle_global_input() {
    // Toggle skills menu
    if (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_OPEN_SKILLS)) {
        g_skills_open = !g_skills_open;
        if (g_skills_open) {
            g_pause_menu_open = false;  // Close pause if opening skills
        }
    }
    
    // Handle skills menu input when open
    if (g_skills_open) {
        skills_handle_menu_input();
    }
}
```

### XP Gain Integration

```c
// Award XP for activities
void award_mining_xp(int amount) {
    int prof = SDK_PROFESSION_MINING;
    g_profession_xp[prof] += amount;
    
    // Check for level up
    while (g_profession_xp[prof] >= g_profession_xp_to_next[prof]) {
        g_profession_xp[prof] -= g_profession_xp_to_next[prof];
        g_profession_levels[prof]++;
        g_profession_points[prof]++;  // +1 skill point per level
        g_profession_xp_to_next[prof] = calculate_next_xp_threshold(
            g_profession_levels[prof]
        );
        
        show_notification("Mining level up! Level %d", 
                         g_profession_levels[prof]);
    }
}
```

### Combat/Gameplay Effects

```c
// Apply skill effects in combat
float calculate_damage_bonus() {
    float bonus = 0.0f;
    
    // Melee damage skill (row 0 in combat skills)
    int melee_rank = g_combat_skill_ranks[0];
    bonus += melee_rank * 0.05f;  // +5% per rank
    
    return bonus;
}

// Apply survival effects
float calculate_gathering_speed() {
    // Gathering skill (row 2 in survival skills)
    int gather_rank = g_survival_skill_ranks[2];
    return 1.0f + (gather_rank * 0.1f);  // +10% per rank
}
```

### Save/Load Integration

```c
void save_skills(FILE* file) {
    // General skills
    fwrite(g_combat_skill_ranks, 1, sizeof(g_combat_skill_ranks), file);
    fwrite(g_survival_skill_ranks, 1, sizeof(g_survival_skill_ranks), file);
    
    // Professions
    fwrite(g_profession_points, sizeof(int), SDK_PROFESSION_COUNT, file);
    fwrite(g_profession_levels, sizeof(int), SDK_PROFESSION_COUNT, file);
    fwrite(g_profession_xp, sizeof(int), SDK_PROFESSION_COUNT, file);
    fwrite(g_profession_xp_to_next, sizeof(int), SDK_PROFESSION_COUNT, file);
    fwrite(g_profession_ranks, 1, sizeof(g_profession_ranks), file);
}

void load_skills(FILE* file) {
    skills_reset_progression();  // Start fresh
    
    // Read saved data...
    fread(g_combat_skill_ranks, 1, sizeof(g_combat_skill_ranks), file);
    // ... etc
}
```

---

## AI Context Hints

### Adding New Combat/Survival Skills

1. **Increase count constant:**
   ```c
   #define SDK_COMBAT_SKILL_COUNT 9  // Was 8
   ```

2. **Update skill definitions** (in appropriate header):
   ```c
   typedef enum {
       SDK_COMBAT_SKILL_MELEE_DAMAGE = 0,
       SDK_COMBAT_SKILL_RANGED_DAMAGE,
       // ... existing ...
       SDK_COMBAT_SKILL_NEW_ABILITY,  // Add at end
       SDK_COMBAT_SKILL_COUNT
   } SdkCombatSkill;
   ```

3. **Update UI strings** in skills menu renderer:
   ```c
   const char* get_combat_skill_name(int row) {
       switch (row) {
           case 0: return "Melee Damage";
           // ...
           case 8: return "New Ability";
       }
   }
   ```

4. **Implement effect** in gameplay systems:
   ```c
   float apply_new_ability_effect() {
       int rank = g_combat_skill_ranks[SDK_COMBAT_SKILL_NEW_ABILITY];
       return rank * 0.1f;  // 10% per rank
   }
   ```

### Adding New Professions

1. **Add to profession enum:**
   ```c
   typedef enum {
       // ... existing ...
       SDK_PROFESSION_FISHING,  // New
       SDK_PROFESSION_COUNT
   } SdkProfession;
   ```

2. **Initialize in skills_reset_progression():**
   ```c
   g_profession_xp_to_next[SDK_PROFESSION_FISHING] = 25;
   ```

3. **Add profession data** (name, description, skill tree):
   ```c
   typedef struct {
       const char* name;
       const char* description;
       SdkProfessionSkillNode nodes[SDK_PROFESSION_NODE_COUNT];
   } ProfessionData;
   
   static const ProfessionData g_profession_data[] = {
       [SDK_PROFESSION_MINING] = {"Mining", "Extract resources...", ...},
       [SDK_PROFESSION_FISHING] = {"Fishing", "Catch fish...", ...},
   };
   ```

### Skill Tree Dependencies

For prerequisite-based skill trees:

```c
typedef struct {
    int required_rank;           // Points needed in prerequisite
    int prerequisite_node;       // Parent node index (-1 = none)
} SkillNodePrereq;

bool can_spend_profession_point(int profession, int node) {
    int prereq = g_profession_nodes[profession][node].prerequisite_node;
    if (prereq < 0) return true;  // No prereq
    
    int required = g_profession_nodes[profession][node].required_rank;
    return g_profession_ranks[profession][prereq] >= required;
}
```

### Resetting Skills (Respec)

```c
void respec_skills() {
    // Save current level (don't reset progression)
    int saved_level = g_player_level;
    
    // Reset all skill ranks
    skills_reset_progression();
    
    // Restore level (recalculate available points)
    g_player_level = saved_level;
    
    // Player can now re-allocate all points
    show_notification("Skills reset! Re-allocate your points.");
}
```

### Skill Notification System

```c
void on_skill_rank_increased(int tab, int row, int new_rank) {
    const char* skill_name = get_skill_name(tab, row);
    
    show_notification("%s increased to rank %d!", 
                     skill_name, new_rank);
    
    // Log for analytics
    log_event("skill_rank_up", "{\"skill\":\"%s\",\"rank\":%d}",
              skill_name, new_rank);
}
```

---

## Related Documentation

- `SDK/Core/Entities/` - Player entity, XP system
- `SDK/Core/Input/` - Input handling for skills menu
- `SDK/Core/PauseMenu/` - Pause menu integration
- `SDK/Docs/Core/Input/SDK_Input.md` - Input action constants
- `SDK/Core/World/Settlements/` - NPC skills (if applicable)

---

**Source Files:**
- `SDK/Core/SkillMenu/sdk_skills_menu.c` (7,163 bytes) - Implementation

**Note:** This module has no header file; functions are called from the runtime system.
