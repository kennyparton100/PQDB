#ifndef SDK_STATION_INTERNAL_H
#define SDK_STATION_INTERNAL_H

#include "../API/Internal/sdk_api_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

int station_find_index(int wx, int wy, int wz);
void station_remove_index(int index);
void station_remove_at(int wx, int wy, int wz);
int station_output_accepts(const StationState* state, ItemType result);
int station_can_place_in_slot(BlockType block_type, int slot, ItemType item);
void station_take_output(int index, int take_all);
void station_place_from_hotbar(int index, int slot);
void station_remove_to_hotbar(int index, int slot, int take_all);

#ifdef __cplusplus
}
#endif

#endif
