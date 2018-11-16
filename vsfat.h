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

#ifndef VSFAT_H_INCLUDED
#define VSFAT_H_INCLUDED

#include <linux/limits.h>

//Check bitness of where we're being built and handle appropriately
#if __GNUC__
#if __x86_64__ || __ppc64__
#define ENV64BIT
#else
#define ENV32BIT
#endif
#endif

//Linked list for the DirTables
typedef struct DirTable
{
    unsigned char *dirtable;
    struct DirTable *next;
} DirTable;

//Ultimately, the file_path might move out of here
//But, for now, this is a very straight forward way to handle the mapping

typedef struct Fat_Directory
{
    char *path;
    DirTable *dirtables;
    u_int32_t current_dir_position;
    u_int32_t dir_location;
    struct Fat_Directory *parent;
} Fat_Directory;

typedef struct AddressRegion
{
    u_int64_t base;
    u_int64_t length;
    void *mem_pointer;
    char *file_path;
} AddressRegion;

#pragma pack(push, 1) /* BYTE align in memory (no padding) */
typedef struct BootEntry
{
    u_int8_t BS_jmpBoot[3];
    u_int8_t BS_OEMName[8];
    u_int16_t BPB_BytsPerSec;
    u_int8_t BPB_SecPerClus;
    u_int16_t BPB_RsvdSecCnt;
    u_int8_t BPB_NumFATs;
    u_int16_t BPB_RootEntCnt;
    u_int16_t BPB_TotSec16;
    u_int8_t BPB_Media;
    u_int16_t BPB_FATSz16;
    u_int16_t BPB_SecPerTrk;
    u_int16_t BPB_NumHeads;
    u_int32_t BPB_HiddSec;
    u_int32_t BPB_TotSec32;
    u_int32_t BPB_FATSz32;
    u_int16_t BPB_ExtFlags;
    u_int16_t BPB_FSVer;
    u_int32_t BPB_RootClus;
    u_int16_t BPB_FSInfo;
    u_int16_t BPB_BkBootSec;
    u_int8_t BPB_Reserved[12];
    u_int8_t BS_DrvNum;
    u_int8_t BS_Reserved1;
    u_int8_t BS_BootSig;
    u_int32_t BS_VolID;
    u_int8_t BS_VolLab[11];
    u_int8_t BS_FilSysType[8];
    u_int8_t BS_BootCode32[420];
    u_int16_t BS_BootSign;
} BootEntry;

typedef struct DirEntry
{
    u_int8_t DIR_Name[8];
    u_int8_t DIR_Ext[3];
    u_int8_t DIR_Attr;
    u_int8_t DIR_NTRes;
    u_int8_t DIR_CrtTimeTenth;
    u_int16_t DIR_CrtTime;
    u_int16_t DIR_CrtDate;
    u_int16_t DIR_LstAccDate;
    u_int16_t DIR_FstClusHI;
    u_int16_t DIR_WrtTime;
    u_int16_t DIR_WrtDate;
    u_int16_t DIR_FstClusLO;
    u_int32_t DIR_FileSize;
} DirEntry;

typedef struct LDirEntry
{
    u_int8_t LDIR_Ord;
    unsigned char LDIR_Name1[10]; //Char 1-5
    u_int8_t LDIR_Attr;
    u_int8_t LDIR_Type;
    u_int8_t LDIR_Chksum;
    unsigned char LDIR_Name2[12]; //Char 6-11
    u_int16_t LDIR_FstClusLO;
    unsigned char LDIR_Name3[4]; //Char 12-13
} LDirEntry;

#pragma pack(pop)

#endif /* VSFAT_H_INCLUDED */
