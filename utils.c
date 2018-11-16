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

#include "utils.h"

//Utility function for division that always returns a rounded up value
uint32_t ceil_div(uint32_t x, uint32_t y)
{
        return (x % y) ? x / y + 1 : x / y; //Ceiling division
}

//Output the contents of a boot struct
void printBootSect(BootEntry *bootentry)
{
        fprintf(stderr, "BS_jmpBoot[3] %02x %02x %02x\n",
                bootentry->BS_jmpBoot[0],
                bootentry->BS_jmpBoot[1], bootentry->BS_jmpBoot[2]);

        fprintf(stderr,
                "BS_OEMName[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
                bootentry->BS_OEMName[0], bootentry->BS_OEMName[1],
                bootentry->BS_OEMName[2], bootentry->BS_OEMName[3],
                bootentry->BS_OEMName[4], bootentry->BS_OEMName[5],
                bootentry->BS_OEMName[6], bootentry->BS_OEMName[7],
                bootentry->BS_OEMName);

        fprintf(stderr, "BPB_BytsPerSec=%u\n", bootentry->BPB_BytsPerSec);
        fprintf(stderr, "BPB_SecPerClus=%u\n", bootentry->BPB_SecPerClus);
        fprintf(stderr, "BPB_RsvdSecCnt=%u\n", bootentry->BPB_RsvdSecCnt);
        fprintf(stderr, "BPB_NumFATs=%u\n", bootentry->BPB_NumFATs);
        fprintf(stderr, "BPB_RootEntCnt=%u\n", bootentry->BPB_RootEntCnt);
        fprintf(stderr, "BPB_TotSec16=%u\n", bootentry->BPB_TotSec16);
        fprintf(stderr, "BPB_Media=%u\n", bootentry->BPB_Media);
        fprintf(stderr, "BPB_FATSz16=%u\n", bootentry->BPB_FATSz16);
        fprintf(stderr, "BPB_SecPerTrk=%u\n", bootentry->BPB_SecPerTrk);
        fprintf(stderr, "BPB_NumHeads=%u\n", bootentry->BPB_NumHeads);
        fprintf(stderr, "BPB_HiddSec=%u\n", bootentry->BPB_HiddSec);
        fprintf(stderr, "BPB_TotSec32=%u\n", bootentry->BPB_TotSec32);
        fprintf(stderr, "BPB_FATSz32=%u\n", bootentry->BPB_FATSz32);
        fprintf(stderr, "BPB_ExtFlags=%u\n", bootentry->BPB_ExtFlags);
        fprintf(stderr, "BPB_FSVer=%u\n", bootentry->BPB_FSVer);
        fprintf(stderr, "BPB_RootClus=%u\n", bootentry->BPB_RootClus);
        fprintf(stderr, "BPB_FSInfo=%u\n", bootentry->BPB_FSInfo);
        fprintf(stderr, "BPB_BkBootSec=%u\n", bootentry->BPB_BkBootSec);
        fprintf(stderr, "BPB_Reserved[12] %02x\n", bootentry->BPB_Reserved[12]);
        fprintf(stderr, "BS_DrvNum=%u\n", bootentry->BS_DrvNum);
        fprintf(stderr, "BS_Reserved1=%02x\n", bootentry->BS_Reserved1);
        fprintf(stderr, "BS_BootSig=%02x\n", bootentry->BS_BootSig);
        fprintf(stderr, "BS_VolID=%02x\n", bootentry->BS_VolID);
        fprintf(stderr,
                "BS_VolLab[11] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %.11s\n",
                bootentry->BS_VolLab[0], bootentry->BS_VolLab[1],
                bootentry->BS_VolLab[2], bootentry->BS_VolLab[3],
                bootentry->BS_VolLab[4], bootentry->BS_VolLab[5],
                bootentry->BS_VolLab[6], bootentry->BS_VolLab[7],
                bootentry->BS_VolLab[8], bootentry->BS_VolLab[9],
                bootentry->BS_VolLab[10], bootentry->BS_VolLab);

        fprintf(stderr,
                "BS_FilSysType[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
                bootentry->BS_FilSysType[0], bootentry->BS_FilSysType[1],
                bootentry->BS_FilSysType[2], bootentry->BS_FilSysType[3],
                bootentry->BS_FilSysType[4], bootentry->BS_FilSysType[5],
                bootentry->BS_FilSysType[6], bootentry->BS_FilSysType[7],
                bootentry->BS_FilSysType);

        fprintf(stderr, "\n");
}