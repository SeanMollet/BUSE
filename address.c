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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#include "address.h"
#include "setup.h"
#include "vsfat.h"

AddressRegion *address_regions;
uint32_t address_regions_count;

//Add an address region to the mapped address regions array
void add_address_region(uint64_t base, uint64_t length, void *mem_pointer,
                        char *file_path)
{
    address_regions_count++;
    address_regions =
        realloc(address_regions, address_regions_count * sizeof(AddressRegion));
    address_regions[address_regions_count - 1].base = base;
    address_regions[address_regions_count - 1].length = length;
    address_regions[address_regions_count - 1].mem_pointer = mem_pointer;
    address_regions[address_regions_count - 1].file_path = file_path;
}

//Return a memory address given a fat sector
uint64_t address_from_fatsec(uint32_t fatsec)
{
    return (uint64_t)part1_base +
           (uint64_t)bootentry.BPB_BytsPerSec * fatsec;
}

//Return a memory address given a fat cluster
uint64_t address_from_fatclus(uint32_t fatclus)
{
    return (uint64_t)address_from_fatsec(fat_location(bootentry.BPB_NumFATs)) +
           (uint64_t)bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus * (fatclus - 2);
}

//Find either of the FATS
uint32_t fat_location(uint32_t fatnum)
{
    return bootentry.BPB_RsvdSecCnt + bootentry.BPB_FATSz32 * fatnum;
}

//Reverse lookup a cluster given an address
uint32_t clus_from_addr(uint64_t address)
{
    uint64_t fatbase = address_from_fatclus(root_dir_loc());
    if (address < fatbase)
    {
        return 0;
    }
    //Subtract the base
    address -= fatbase;
    return address / (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus) + 2;
}

//Reverse lookup a fat from an address
uint32_t fat_entry_from_addr(uint64_t address)
{
    //We just use the first fat since we shouldn't be updating anyway
    //Fat entries are 32 bits long, so we *4
    uint32_t cluster = clus_from_addr(address);
    return address_from_fatsec(fat_location(0)) + cluster * 4;
}

//Returns the location of the root_dir
uint32_t root_dir_loc()
{
    //BPB_NumFATs is 1 based, so this actually gives us the end of the last fat
    //return fat_location(bootentry.BPB_NumFATs);
    //Fat hard codes the root directory to be the first sector
    return 2;
}

uint32_t data_loc()
{
    //The formula for this is:
    //DataStartSector = RootDirStartSector + RootDirSectors;
    //RootDirSectors = (32 * BPB_RootEntCnt + BPB_BytsPerSec - 1) / BPB_BytsPerSec;
    //We're 100% FAT32, so BPB_RootEntCnt=0, which makes RootDirSectors = (511)/512 = 1
    return root_dir_loc() + 1;
}