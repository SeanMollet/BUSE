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

#include <stdint.h>
#include <linux/limits.h>

//Check bitness of where we're being built and handle appropriately
#if __GNUC__
#if __x86_64__ || __ppc64__ || __aarch64__
#define ENV64BIT
#else
#define ENV32BIT
#endif
#endif

//Linked list for files in a directory
typedef struct FileEntry
{
    unsigned char Filename[8];
    unsigned char Ext[3];
    struct FileEntry *next;
} FileEntry;

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
    uint32_t current_dir_position;
    uint32_t dir_location;
    FileEntry *files;
    struct Fat_Directory *parent;
} Fat_Directory;

#pragma pack(push, 1) /* BYTE align in memory (no padding) */
typedef struct BootEntry
{
    uint8_t BS_jmpBoot[3];
    uint8_t BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t BPB_Reserved[12];
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1;
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    uint8_t BS_VolLab[11];
    uint8_t BS_FilSysType[8];
    uint8_t BS_BootCode32[420];
    uint16_t BS_BootSign;
} BootEntry;

typedef struct LfnEntry
{
    uint8_t LFN_Seq;
    uint8_t LFN_Name[10];
    uint8_t LFN_Attributes;
    uint8_t LFN_Type;
    uint8_t LFN_Checksum;
    uint8_t LFN_Name2[12];
    uint8_t LFN_Cluster_HI;
    uint8_t LFN_Cluster_LO;
    uint8_t LFN_Name3[4];
} LfnEntry;

typedef struct DirEntry
{
    uint8_t DIR_Name[8];
    uint8_t DIR_Ext[3];
    uint8_t DIR_Attr;
    uint8_t DIR_NTRes;
    uint8_t DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} DirEntry;

typedef struct LDirEntry
{
    uint8_t LDIR_Ord;
    unsigned char LDIR_Name1[10]; //Char 1-5
    uint8_t LDIR_Attr;
    uint8_t LDIR_Type;
    uint8_t LDIR_Chksum;
    unsigned char LDIR_Name2[12]; //Char 6-11
    uint16_t LDIR_FstClusLO;
    unsigned char LDIR_Name3[4]; //Char 12-13
} LDirEntry;

typedef struct FSInfo
{
    uint32_t FSI_LeadSig;
    uint8_t FSI_Reserved1[480];
    uint32_t FSI_StrucSig;
    uint32_t FSI_Free_Count;
    uint32_t FSI_Nxt_Free;
    uint8_t FSI_Reserved2[12];
    uint32_t FSI_TrailSig;
} FSInfo;

#pragma pack(pop)

//Global variables
extern BootEntry bootentry;
extern uint32_t *fat;
#endif /* VSFAT_H_INCLUDED */
