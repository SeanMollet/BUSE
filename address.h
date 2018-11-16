/*
 * vsfat - virtual synthetic FAT filesystem on network block device from local folder
 * Copyright (C) 2017 Sean Mollet
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <ctype.h>
#include <stdint.h>

void add_address_region(uint64_t base, uint64_t length,
                        void *mem_pointer, char *file_path);
uint64_t address_from_fatsec(uint32_t fatclus);
uint64_t address_from_fatclus(uint32_t fatclus);
uint32_t fat_location(uint32_t fatnum);

uint32_t root_dir_loc();

typedef struct AddressRegion
{
    uint64_t base;
    uint64_t length;
    void *mem_pointer;
    char *file_path;
} AddressRegion;

extern AddressRegion *address_regions;
extern uint32_t address_regions_count;