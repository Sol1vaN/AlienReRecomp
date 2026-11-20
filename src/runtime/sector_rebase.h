#pragma once
#include <stdint.h>
typedef struct { int32_t p[3];uint16_t angle[3];uint32_t model;uint8_t kind; } SectorDoor;
typedef struct { SectorDoor door[96];unsigned count;int room; } SectorDoors;
unsigned sector_rebase_align(uint32_t transfer);
void sector_rebase_doors_capture(uint32_t transfer,SectorDoors *state);
unsigned sector_rebase_doors_restore(const SectorDoors *state);
