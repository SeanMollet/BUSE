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

#include "buse.h"
#include "vsfat.h"
#include "Fat32_Attr.h"
#include "utils.h"

static unsigned char *mbr;
static const u_int32_t part1_base = 1048576;

static BootEntry bootentry;

static u_int32_t *fat = 0;
static u_int32_t current_fat_position = 3; // 0 and 1 are special and 2 is the root dir
static unsigned char fat_end[] = {0xFF, 0xFF, 0xFF, 0xFF};

Fat_Directory root_dir;
Fat_Directory *current_dir;

static AddressRegion *address_regions;
static u_int32_t address_regions_count;

static int xmpl_debug = 1;

static int xmp_read(void *buf, u_int32_t len, u_int64_t offset,
                    void *userdata);
static int xmp_write(const void *buf, u_int32_t len, u_int64_t offset,
                     void *userdata);
static void xmp_disc(void *userdata);
static int xmp_flush(void *userdata);
static int xmp_trim(u_int64_t from, u_int32_t len, void *userdata);
static void add_address_region(u_int64_t base, u_int64_t length,
                               void *mem_pointer, char *file_path);
static u_int64_t address_from_fatclus(u_int32_t fatclus);
static u_int32_t fat_location(u_int32_t fatnum);

static u_int32_t root_dir_loc();

static struct buse_operations aop = {
    .read = xmp_read,
    .write = xmp_write,
    .disc = xmp_disc,
    .flush = xmp_flush,
    .trim = xmp_trim,
    .blksize = 512,
    .size_blocks = 4292870144,
};

//API Functions
static int xmp_read(void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  if (*(int *)userdata)
  {
#if defined(ENV64BIT)
    fprintf(stderr, "Read %#x bytes from  %#lx\n", len, offset);
#else
    fprintf(stderr, "Read %#x bytes from  %#llx\n", len, offset);
#endif
  }
  //Make sure the buffer is zeroed
  memset(buf, 0, len);
  //Check if this read falls within a mapped area
  //No point doing this if we've already used up len
  for (u_int32_t a = 0; a < address_regions_count && len > 0; a++)
  {
    if ((offset >= address_regions[a].base && // See if the beginning is inside our range
         offset <= address_regions[a].base + address_regions[a].length) ||
        (offset + len >= address_regions[a].base && // See if the end is (we'll also accept both)
         offset + len <= address_regions[a].base + address_regions[a].length) ||
        (offset <= address_regions[a].base && //Or, we're entirely contained within it
         offset + len >= address_regions[a].base + address_regions[a].length))
    {
      u_int32_t uselen = len;
      u_int32_t usepos;
      u_int32_t usetarget;

      //Figure out who's first and set the offset variables accordingly
      if (offset < address_regions[a].base)
      {
        usepos = 0;
        usetarget = address_regions[a].base - offset;
      }
      else
      {
        usepos = offset - address_regions[a].base;
        usetarget = 0;
      }

      //If they're only asking for part of what we have
      if (address_regions[a].base + address_regions[a].length >= offset + len)
      {
        uselen = offset + len - address_regions[a].base;
      }

      //Make sure we don't go off the end
      if (uselen > address_regions[a].length - usepos)
      {
        uselen = address_regions[a].length - usepos;
      }
      //Or give them more than what they want
      if (uselen > len)
      {
        uselen = len;
      }

      if (*(int *)userdata)
      {
#if defined(ENV64BIT)
        fprintf(stderr,
                "base: %#lx length: %#lx usepos: %#x offset: %#lx len: %#x usetarget: %#x uselen: %#x\n",
                address_regions[a].base, address_regions[a].length,
                usepos, offset, len, usetarget, uselen);
#else
        fprintf(stderr,
                "base: %#llx length: %#llx usepos: %#x offset: %#llx len: %#x usetarget: %#x uselen: %#x\n",
                address_regions[a].base, address_regions[a].length,
                usepos, offset, len, usetarget, uselen);

#endif
      }

      //For real memory mapped stuff
      if (address_regions[a].mem_pointer)
      {
        memcpy((unsigned char *)buf + usetarget,
               (unsigned char *)address_regions[a].mem_pointer + usepos, uselen);
        len = len - (uselen + usetarget);
        offset += uselen + usetarget;
        buf = (unsigned char *)buf + uselen + usetarget;
      }
      else //Mapped in file
      {
        if (address_regions[a].file_path)
        {
          FILE *fd = fopen(address_regions[a].file_path, "rb");
          if (fd)
          {

            fseek(fd, usepos, SEEK_SET);
            size_t read_count = fread((unsigned char *)buf + usetarget, uselen, 1, fd);
            fclose(fd);
            if (*(int *)userdata)
            {
#if defined(ENV64BIT)
              fprintf(stderr,
                      "file: %s pos: %u len: %u read_count: %lu\n", address_regions[a].file_path, usepos, uselen, read_count);
#else
              fprintf(stderr,
                      "file: %s pos: %u len: %u read_count: %u\n", address_regions[a].file_path, usepos, uselen, read_count);
#endif
            }
            //Up above, we already made sure we have enough data available
            //So, we either got it or we didn't, either way, we assume we did and move on
            //If we didn't, something will blow up, but that's a tolerable behavior
            len = len - (uselen + usetarget);
            offset += uselen + usetarget;
            buf = (unsigned char *)buf + uselen + usetarget;
          }
        }
      }
    }
  }
  //If we've gotten here, we've used up all the mapped in areas, so fill the rest with 0s
  memset(buf, 0, len);
  //Pedantry
  buf = (unsigned char *)buf + len;
  len = 0;
  return 0;
}

static int xmp_write(const void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  (void)buf;
  if (*(int *)userdata)
  {
#if defined(ENV64BIT)
    fprintf(stderr, "W - %lu, %u\n", offset, len);
#else
    fprintf(stderr, "W - %llu, %u\n", offset, len);
#endif
  }

  if (offset > (u_int64_t)aop.blksize * aop.size_blocks)
  {
    return 0;
  }
  //memcpy((char *)data + offset, buf, len);
  return 0;
}

static void xmp_disc(void *userdata)
{
  if (*(int *)userdata)
    fprintf(stderr, "Received a disconnect request.\n");
}

static int xmp_flush(void *userdata)
{
  if (*(int *)userdata)
    fprintf(stderr, "Received a flush request.\n");
  return 0;
}

static int xmp_trim(u_int64_t from, u_int32_t len, void *userdata)
{
  if (*(int *)userdata)
  {
#if defined(ENV64BIT)
    fprintf(stderr, "T - %lu, %u\n", from, len);
#else
    fprintf(stderr, "T - %llu, %u\n", from, len);
#endif
  }
  return 0;
}

//Add an address region to the mapped address regions array
static void add_address_region(u_int64_t base, u_int64_t length, void *mem_pointer,
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

//Build and map the MBR. Note that we just use a fixed 2TB size
static void build_mbr()
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
static void build_boot_sector()
{

  //Build our bootsector
  bootentry.BS_jmpBoot[0] = 0xeb;
  bootentry.BS_jmpBoot[1] = 0x58;
  bootentry.BS_jmpBoot[2] = 0x90;
  strncpy((char *)&bootentry.BS_OEMName, "MSDOS5.0", 8);
  //
  bootentry.BPB_BytsPerSec = 512;
  //Eventually this will need to be turned up to get full capacity
  bootentry.BPB_SecPerClus = 1;
  bootentry.BPB_RsvdSecCnt = 32;
  bootentry.BPB_NumFATs = 2;
  bootentry.BPB_RootEntCnt = 0;
  bootentry.BPB_TotSec16 = 0;
  bootentry.BPB_Media = 248;
  bootentry.BPB_FATSz16 = 0;
  bootentry.BPB_SecPerTrk = 32;
  bootentry.BPB_NumHeads = 64;
  bootentry.BPB_HiddSec = 0;
  bootentry.BPB_TotSec32 = 102400;
  bootentry.BPB_FATSz32 = 788;
  bootentry.BPB_ExtFlags = 0;
  bootentry.BPB_FSVer = 0;
  bootentry.BPB_RootClus = 2;
  bootentry.BPB_FSInfo = 1;
  bootentry.BPB_BkBootSec = 6;
  bootentry.BS_DrvNum = 128;
  bootentry.BS_Reserved1 = 0;
  bootentry.BS_BootSig = 29;
  bootentry.BS_VolID = 0x8456f237;
  bootentry.BS_BootSign = 0xAA55;

  unsigned char vol[] =
      {0x56, 0x53, 0x46, 0x41, 0x54, 0x46, 0x53, 0x20, 0x20, 0x20, 0x20};
  unsigned char fstype[] = {0x46, 0x41, 0x54, 0x33, 0x32, 0x20, 0x20, 0x20};

  memcpy(&bootentry.BS_VolLab, vol, sizeof(vol));
  memcpy(&bootentry.BS_FilSysType, fstype, sizeof(fstype));
  if (xmpl_debug)
  {
    printBootSect(bootentry);
    fprintf(stderr, "BS 1: %x\n", part1_base);
    fprintf(stderr, "BS 2: %x\n",
            part1_base +
                bootentry.BPB_BkBootSec * bootentry.BPB_BytsPerSec);
  }

  //Main copy
  add_address_region(part1_base, 512, &bootentry, 0);
  add_address_region(part1_base +
                         bootentry.BPB_BkBootSec * bootentry.BPB_BytsPerSec,
                     512,
                     &bootentry, 0);
}

//Return a memory address given a fat sector
static u_int64_t address_from_fatsec(u_int32_t fatsec)
{
  return (u_int64_t)part1_base +
         (u_int64_t)bootentry.BPB_BytsPerSec * fatsec;
}

//Return a memory address given a fat cluster
static u_int64_t address_from_fatclus(u_int32_t fatclus)
{
  return (u_int64_t)address_from_fatsec(fat_location(bootentry.BPB_NumFATs)) +
         (u_int64_t)bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus * (fatclus - 2);
}

//Find either of the FATS
static u_int32_t fat_location(u_int32_t fatnum)
{
  return bootentry.BPB_RsvdSecCnt + bootentry.BPB_FATSz32 * fatnum;
}

//Reverse lookup a cluster given an address
static u_int32_t clus_from_addr(u_int64_t address)
{
  u_int64_t fatbase = address_from_fatclus(root_dir_loc());
  if (address < fatbase)
  {
    return 0;
  }
  //Subtract the base
  address -= fatbase;
  return address / (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus) + 2;
}

//Reverse lookup a fat from an address
static u_int32_t fat_entry_from_addr(u_int64_t address)
{
  //We just use the first fat since we shouldn't be updating anyway
  //Fat entries are 32 bits long, so we *4
  u_int32_t cluster = clus_from_addr(address);
  return address_from_fatsec(fat_location(0)) + cluster * 4;
}

//Returns the location of the root_dir
static u_int32_t root_dir_loc()
{
  //BPB_NumFATs is 1 based, so this actually gives us the end of the last fat
  //return fat_location(bootentry.BPB_NumFATs);
  //Fat hard codes the root directory to be the first sector
  return 2;
}

static u_int32_t data_loc()
{
  //The formula for this is:
  //DataStartSector = RootDirStartSector + RootDirSectors;
  //RootDirSectors = (32 * BPB_RootEntCnt + BPB_BytsPerSec - 1) / BPB_BytsPerSec;
  //We're 100% FAT32, so BPB_RootEntCnt=0, which makes RootDirSectors = (511)/512 = 1
  return root_dir_loc() + 1;
}

//Do the initial FAT setup and mapping
static void build_fats()
{
  //These first two entries are part of the spec
  //The third marks our root directly
  unsigned char fatspecial[] = {0xF8, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF, 0xFF, 0x0F, 0xF8, 0xFF, 0xFF, 0x0F};

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

  root_dir.path = "\\";
  root_dir.current_dir_position = 0;
  root_dir.dirtables = 0;
  //This makes sure we can never go above the root_dir
  root_dir.parent = &root_dir;
  root_dir.dir_location = root_dir_loc();

  current_dir = &root_dir;
}

//Advance the pointer to the next free sector in the fat
static void fat_find_free()
{
  while (fat[current_fat_position] != 0)
  {
    current_fat_position++;
    //Bail if we go off the end
    if (current_fat_position >= (bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec) / 4)
    {
      return;
    }
  }
}

//Pass in either a memory segment or a filepath
//This will load the proper mappings and configure the fat
//Directory entries should be handled above here
//This assumes we have 0 fragmentation (which should always be true, since we don't allow deleting)
static int fat_new_file(unsigned char *data, char *filepath, u_int32_t length)
{
  //Get a free cluster
  fat_find_free();
  //length always has to be at least 1
  if (length <= 0)
  {
    length = 1;
  }

  //Make sure we have enough space
  u_int32_t cluster_size = (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus);
  u_int32_t clusters_required = ceil_div(length, cluster_size); //Ceiling division
  if (clusters_required >
      ((bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec) / 4) - current_fat_position)
  { //Free clusters
    return -1;
  }
  add_address_region(address_from_fatclus(current_fat_position), length, data, filepath);

  //Remove the space for our first cluster
  if (length > cluster_size)
  {
    length -= cluster_size;
  }
  else
  {
    length = 0;
  }

  //Mark any additional sectors in the FAT that we're using
  while (length > 0)
  {
    fat[current_fat_position] = current_fat_position + 1;
    current_fat_position++;
    //Unsigned, so the subtraction could loop us around
    if (length > cluster_size)
    {
      length -= cluster_size;
    }
    else
    {
      length = 0;
    }
  }

  memcpy(&fat[current_fat_position], fat_end, sizeof(fat_end)); // Terminate this chain
  return 0;
}

//This does accept multiple entries. However, it does not accept more than a single file worth
static int dir_add_entry(unsigned char *entry, u_int32_t length)
{
  u_int32_t cluster_size = (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus);
  u_int32_t entrys_per_cluster = cluster_size / sizeof(DirEntry);
  u_int32_t current_cluster_free = current_dir->current_dir_position % entrys_per_cluster;

  //Make sure we have an initial entry
  if (current_dir->dirtables == 0)
  {
    current_dir->dirtables = malloc(sizeof(DirTable));
    current_dir->dirtables->next = 0;

    current_dir->dirtables->dirtable = malloc(cluster_size);
    u_int64_t dest = address_from_fatclus(current_dir->dir_location);
    add_address_region(dest, cluster_size, current_dir->dirtables->dirtable, 0);
    current_cluster_free = entrys_per_cluster;

    memcpy(&fat[current_dir->dir_location], fat_end, sizeof(fat_end)); // Terminate this chain in the FAT
  }

  //We don't let re-alloc do this, because we need to grab cluster 2 for the first one
  //We also override the current_cluster_free because the 0 breaks it
  //Make sure the DIR exists
  if (current_dir->dirtables->dirtable == 0)
  {
  }

  //Make sure we don't exceed the 2Mb limit for directory size
  if (current_dir->current_dir_position + length > (1024 * 1024 * 2) / sizeof(DirEntry))
  {
    return -1;
  }

  //Add another cluster if needed
  if (current_cluster_free < length)
  {
    fat_find_free();

    u_int32_t used_clusters = ceil_div(current_dir->current_dir_position, entrys_per_cluster);

    //Add a new entry to the dirtables linked list
    //Find the end of the list
    DirTable *final_dir_table = current_dir->dirtables;
    while (final_dir_table->next != 0)
    {
      final_dir_table = final_dir_table->next;
    }

    final_dir_table->next = malloc(sizeof(DirTable));
    final_dir_table = final_dir_table->next;

    final_dir_table->dirtable = malloc(cluster_size);
    final_dir_table->next = 0;
    add_address_region(address_from_fatclus(current_fat_position), cluster_size, final_dir_table->dirtable, 0);

    //Update the fat for the previous link in the chain to point to the new one
    fat[current_dir->dir_location] = current_fat_position;
    //Advance this pointer to the extended fat sector
    current_dir->dir_location = current_fat_position;
    memcpy(&fat[current_dir->dir_location], fat_end, sizeof(fat_end)); // Terminate this chain in the FAT
  }

  //Find the last dir_table in this chain and the last unused position
  DirTable *final_dir_table = current_dir->dirtables;
  u_int32_t final_table_pos = current_dir->current_dir_position;
  while (final_dir_table->next != 0)
  {
    final_dir_table = final_dir_table->next;
    final_table_pos -= entrys_per_cluster;
  }

  //copy the data in
  memcpy(final_dir_table->dirtable + final_table_pos * sizeof(DirEntry), entry, length * sizeof(DirEntry));
  current_dir->current_dir_position += length;
  //Profit!
  return 0;
}

//Long filename checksum of SFN
static unsigned char fn_checksum(unsigned char *filename)
{
  u_int8_t filename_len;
  unsigned char sum;

  sum = 0;
  for (filename_len = 11; filename_len != 0; filename_len--)
  {
    // NOTE: The operation is an unsigned char rotate right
    sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *filename++;
  }
  return (sum);
}

//Convert filename to FAT32 8.3 format
static void format_name_83(char *input, u_int32_t length, unsigned char *filename, unsigned char *ext)
{
  int32_t period = -1;
  //First we take out things we know don't belong
  for (int a = 0; a < (int32_t)length; a++)
  {
    if (input[a] == 0x20)
    {                  // Space
      input[a] = 0x5F; // _
    }
    if (input[a] == 0xe5)
    {
      input[a] = 0x05;
    }
    input[a] = toupper(input[a]); //This does nothing if there isn't an upper case form
    //Check if this is a period
    if (input[a] == 46)
    {
      period = a;
    }
  }

  //If we didn't get a period, just run to the length
  if (period < 0)
  {
    period = length;
  }

  for (int a = 0; a < 3; a++)
  {
    //Note: the +1 is needed because period= the actual period, not the extension
    if (period + a + 1 < (int32_t)length)
    {
      ext[a] = input[period + a + 1];
    }
    else
    {
      ext[a] = 0x20;
    }
  }

  for (int a = 0; a < 8; a++)
  {
    if (a < period)
    {
      filename[a] = input[a];
    }
    else
    {
      filename[a] = 0x20;
    }
  }
}

static void up_dir()
{
  //Free the current dir (we shouldn't be leaving until we're done with it)
  //Commented out for debugging
  // if (current_dir != &root_dir)
  // {
  //   free(current_dir->path);
  //   free(current_dir);
  // }

  //If the parent is root, we just stay at the root
  current_dir = current_dir->parent;
}

//Add a file to the mapping space
static void add_file(char *name, char *filepath, u_int32_t size, u_char isDirectory)
{
  DirEntry entry;
  //Make sure it's clear
  memset(&entry, 0, sizeof(DirEntry));

  //For now, just stupid 8.3
  format_name_83(name, strlen(name), entry.DIR_Name, entry.DIR_Ext);

  /*  memcpy(entry.DIR_Ext,name + strlen(name)-3,3);
  memcpy(entry.DIR_Name,name,8);

  format_name(entry.DIR_Ext,3);
  format_name(entry.DIR_Name,8);
*/

  //If this is a directory, add the directory bit
  if (isDirectory)
  {
    entry.DIR_Attr |= DIR_Attr_Directory;
  }
  else
  {
    //Set the "Archive" bit
    entry.DIR_Attr = DIR_Attr_Archive;
  }

  //entry.DIR_NRRes = 0x08 | 0x10; //Everything is lowercase

  fat_find_free();
  entry.DIR_FstClusLO = (u_int16_t)(current_fat_position & 0x00FF);
  entry.DIR_FstClusHI = (u_int16_t)(current_fat_position & 0xFF00) >> 16;
  entry.DIR_FileSize = size;

  //dir_add_entry wants a byte array so it can have multiple entries chained together
  //We just cast the struct pointer and tell it how many there are (1 for short filename)

  if (dir_add_entry((unsigned char *)&entry, 1) == 0)
  {
    if (!isDirectory)
    {
      //Allocate the array for the actual data
      fat_new_file(0, filepath, size);
    }
    else
    {
      //Make a new directory entry and change to it
      Fat_Directory *new_dir = malloc(sizeof(Fat_Directory));
      new_dir->path = filepath;
      new_dir->dirtables = 0;
      new_dir->current_dir_position = 0;
      new_dir->parent = current_dir;
      new_dir->dir_location = current_fat_position;
      current_dir = new_dir;

      //Add the . and .. entries to this dir

      DirEntry dotentry;
      //Adding the . entry automatically maps the memory space for the first cluster of this
      //dir entry
      // .
      memset(&dotentry, 0, sizeof(DirEntry));
      dotentry.DIR_Attr = DIR_Attr_Archive | DIR_Attr_Directory;
      dotentry.DIR_Name[0] = '.';
      dotentry.DIR_FstClusLO = (u_int16_t)(current_fat_position & 0x00FF);
      dotentry.DIR_FstClusHI = (u_int16_t)(current_fat_position & 0xFF00) >> 16;
      dir_add_entry((unsigned char *)&dotentry, 1);

      //..
      dotentry.DIR_Name[1] = '.';
      dotentry.DIR_FstClusLO = (u_int16_t)(current_dir->parent->dir_location & 0x00FF);
      dotentry.DIR_FstClusHI = (u_int16_t)(current_dir->parent->dir_location & 0xFF00) >> 16;
      dir_add_entry((unsigned char *)&dotentry, 1);
    }
  }
}

//Scan a given folder and recursively add it to the memory space
static void scan_folder(char *path)
{
  DIR *d = opendir(path);
  struct stat st;
  if (d == NULL)
    return;
  struct dirent *dir;
  while ((dir = readdir(d)) != NULL)
  {
    if (dir->d_type != DT_DIR)
    {
      if (strlen(dir->d_name) + strlen(path) + 1 < PATH_MAX) // If our full path exceeds the allowable length, drop this file
      {
        char *f_path;
        //TODO: Check actual memory usage with large numbers of files
        f_path = malloc(strlen(dir->d_name) + strlen(path) + 2);

        sprintf(f_path, "%s/%s", path, dir->d_name);
        stat(f_path, &st);
        printf("%s/%s  %lu\n", path, dir->d_name, st.st_size);
        add_file(dir->d_name, f_path, st.st_size, 0);
      }
      else
      {
        fprintf(stderr, "File %s/%s path is too long\n", path, dir->d_name);
      }
    }
    else
    {
      if (dir->d_type == DT_DIR && strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) // skip . and ..
      {
        printf("Dir: %s/%s\n", path, dir->d_name);

        char *d_path;
        d_path = malloc(strlen(dir->d_name) + strlen(path) + 2);
        sprintf(d_path, "%s/%s", path, dir->d_name);

        //Create a new dir in the FS and enter it
        add_file(dir->d_name, d_path, 0, 1);
        scan_folder(d_path); // recursive
        up_dir();
      }
    }
  }
  closedir(d);
}

int main(int argc, char *argv[])
{
  if (argc < 3)
  {
    fprintf(stderr,
            "Usage:\n"
            "  %s /dev/nbd0 ./folder_to_export\n"
            "Don't forget to load nbd kernel module (`modprobe nbd`) and\n"
            "run example from root.\n",
            argv[0]);
    return 1;
  }

  build_mbr();
  build_boot_sector();
  build_fats();
  scan_folder(argv[2]);

  return buse_main(argv[1], &aop, (void *)&xmpl_debug);
}
