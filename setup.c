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

#include "setup.h"
#include "utils.h"
#include "address.h"
#include "vsfat.h"

//Create the root directory entry and set it as the current directory
void build_root_dir()
{
  root_dir.path = "\\";
  root_dir.current_dir_position = 0;
  root_dir.dirtables = 0;
  root_dir.files = 0;
  //This makes sure we can never go above the root_dir
  root_dir.parent = &root_dir;
  root_dir.dir_location = root_dir_loc();

  current_dir = &root_dir;
  current_fat_position = root_dir_loc();
}

//Build and map the MBR. Note that we just use a fixed 2TB size
void build_mbr()
{
  unsigned char bootcode[] =
      {0xFA, 0xB8, 0x00, 0x10, 0x8E, 0xD0, 0xBC, 0x00, 0xB0, 0xB8, 0x00, 0x00,
       0x8E, 0xD8, 0x8E, 0xC0, 0xFB, 0xBE, 0x00, 0x7C, 0xBF, 0x00, 0x06, 0xB9, 0x00, 0x02, 0xF3, 0xA4,
       0xEA, 0x21, 0x06, 0x00, 0x00, 0xBE, 0xBE, 0x07, 0x38, 0x04, 0x75, 0x0B, 0x83, 0xC6, 0x10, 0x81,
       0xFE, 0xFE, 0x07, 0x75, 0xF3, 0xEB, 0x16, 0xB4, 0x02, 0xB0, 0x01, 0xBB, 0x00, 0x7C, 0xB2, 0x80,
       0x8A, 0x74, 0x01, 0x8B, 0x4C, 0x02, 0xCD, 0x13, 0xEA, 0x00, 0x7C, 0x00, 0x00, 0xEB, 0xFE, 0x00};
  unsigned char serial[] = {0xDE, 0xAB, 0xBE, 0xEF};
  unsigned char parts[4][16] =
      {{0x00, 0x20, 0x21, 0x00, 0x0c, 0xcd, 0xfb, 0xd2, 0x00, 0x08, 0x00, 0x00,
        0x00, 0xf8, 0xdf, 0xff},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00},
       {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00}};
  unsigned char footer[] = {0x55, 0xAA};

  mbr = malloc(512);
  memset(mbr, 0, 512);

  memcpy(mbr, &bootcode, sizeof(bootcode));
  memcpy(mbr + 440, &serial, sizeof(serial));

  for (int a = 0; a < 4; a++)
  {
    memcpy(mbr + 446 + 16 * a, &parts[a], 16);
  }
  memcpy(mbr + 510, &footer, sizeof(footer));
  add_address_region(0, 512, mbr, 0);
}

//Build and map the bootsector(s)
//Returns the total disk size
uint32_t build_boot_sector(BootEntry *bootentry, int xmpl_debug)
{

  //Build our bootsector
  bootentry->BS_jmpBoot[0] = 0xeb;
  bootentry->BS_jmpBoot[1] = 0x58;
  bootentry->BS_jmpBoot[2] = 0x90;
  strncpy((char *)&bootentry->BS_OEMName, "VSFAT1.0", 8);
  //
  bootentry->BPB_BytsPerSec = 512;
  //Eventually this will need to be turned up to get full capacity
  bootentry->BPB_SecPerClus = Fat32_Sectors_per_Cluster;
  bootentry->BPB_RsvdSecCnt = 32;
  bootentry->BPB_NumFATs = 2;
  bootentry->BPB_RootEntCnt = 0;
  bootentry->BPB_TotSec16 = 0;
  bootentry->BPB_Media = 248;
  bootentry->BPB_FATSz16 = 0;
  bootentry->BPB_SecPerTrk = 32;
  bootentry->BPB_NumHeads = 64;
  bootentry->BPB_HiddSec = 0;
  //8192 = 2Tb with 64 Sec/Clus
  //Subtract 3 for the reserved area and space taken by fats
  //Practical maximum is 8189
  bootentry->BPB_FATSz32 = 8189;
  //This is calculated based on the maximum available with the given FATSize
  uint32_t datasize = bootentry->BPB_FATSz32 *
                      bootentry->BPB_SecPerClus * (bootentry->BPB_BytsPerSec / 4);
  //BPB_RsvdSecCnt includes the boot sector and the FSInfo sector
  //Drivers expect the FATS to start at BPB_RsvdSecCnt*BPB_BytsPerSec
  uint32_t headersize = bootentry->BPB_RsvdSecCnt +
                        (bootentry->BPB_FATSz32 * bootentry->BPB_SecPerClus);
  bootentry->BPB_TotSec32 = datasize + headersize;
  bootentry->BPB_ExtFlags = 0;
  bootentry->BPB_FSVer = 0;
  bootentry->BPB_RootClus = 2;
  bootentry->BPB_FSInfo = 1;
  bootentry->BPB_BkBootSec = 6;
  bootentry->BS_DrvNum = 128;
  bootentry->BS_Reserved1 = 0;
  bootentry->BS_BootSig = 29;
  bootentry->BS_VolID = 0x8456f237;
  bootentry->BS_BootSign = 0xAA55;

  //Update the partition table with our actual size
  //Get the start of the partition
  uint32_t PartStart = 0;
  memcpy(&PartStart, mbr + 454, 4);

  uint32_t PartSize = bootentry->BPB_TotSec32;
  memcpy(mbr + 458, &PartSize, 4);

  //Since we only have a single partition and the MBR, etc are before start
  //This gives us the total sectors
  uint32_t DiskSize = PartSize + PartStart;

  unsigned char vol[] =
      {0x56, 0x53, 0x46, 0x41, 0x54, 0x46, 0x53, 0x20, 0x20, 0x20, 0x20};
  unsigned char fstype[] = {0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20};

  memcpy(&bootentry->BS_VolLab, vol, sizeof(vol));
  memcpy(&bootentry->BS_FilSysType, fstype, sizeof(fstype));
  if (xmpl_debug)
  {
    printBootSect(bootentry);
    fprintf(stderr, "BS 1: %x\n", part1_base);
    fprintf(stderr, "BS 2: %x\n",
            part1_base +
                bootentry->BPB_BkBootSec * bootentry->BPB_BytsPerSec);
  }

  //Main copy
  add_address_region(part1_base, 512, bootentry, 0);
  add_address_region(part1_base +
                         bootentry->BPB_BkBootSec * bootentry->BPB_BytsPerSec,
                     512,
                     bootentry, 0);

  //Microsoft constants for the signatures
  FSInfo *fsi = malloc(sizeof(FSInfo));
  memset(fsi, 0, sizeof(FSInfo));
  fsi->FSI_LeadSig = 0x41615252;
  fsi->FSI_StrucSig = 0x61417272;
  fsi->FSI_Free_Count = 0;
  fsi->FSI_Nxt_Free = 0;
  fsi->FSI_TrailSig = 0xAAAA5555;

  add_address_region(part1_base +
                         bootentry->BPB_FSInfo * bootentry->BPB_BytsPerSec,
                     512,
                     fsi, 0);
  return DiskSize;
}

//Do the initial FAT setup and mapping
void build_fats()
{
  //These first two entries are part of the spec
  unsigned char fatspecial[] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F};

  fat = malloc(bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec);
  memset(fat, 0, bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec);

  memcpy(fat, fatspecial, sizeof(fatspecial));

//There are two copies of the fat, we map the same memory into both
#if defined(ENV64BIT)
  printf("fat0: %lx\n", address_from_fatsec(fat_location(0)));
  printf("fat1: %lx\n", address_from_fatsec(fat_location(1)));
#else
  printf("fat0: %llx\n", address_from_fatsec(fat_location(0)));
  printf("fat1: %llx\n", address_from_fatsec(fat_location(1)));
#endif
  add_address_region(address_from_fatsec(fat_location(0)),
                     bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec, fat,
                     0);
  add_address_region(address_from_fatsec(fat_location(1)),
                     bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec, fat,
                     0);
}