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
#include "setup.h"
#include "address.h"
#include "fatfiles.h"

//Global variables
BootEntry bootentry;
uint32_t *fat = 0;
uint32_t current_fat_position; // 0 and 1 are special and 2 is the root dir
Fat_Directory root_dir;
Fat_Directory *current_dir;
unsigned char *mbr;

//Debug flag
static int xmpl_debug = 0;

//Function prototypes for API
static int xmp_read(void *buf, uint32_t len, uint64_t offset,
                    void *userdata);
static int xmp_write(const void *buf, uint32_t len, uint64_t offset,
                     void *userdata);
static void xmp_disc(void *userdata);
static int xmp_flush(void *userdata);
static int xmp_trim(uint64_t from, uint32_t len, void *userdata);

//API Configuration struct
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
static int xmp_read(void *buf, uint32_t len, uint64_t offset, void *userdata)
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
  for (uint32_t a = 0; a < address_regions_count && len > 0; a++)
  {
    if ((offset >= address_regions[a].base && // See if the beginning is inside our range
         offset <= address_regions[a].base + address_regions[a].length) ||
        (offset + len >= address_regions[a].base && // See if the end is (we'll also accept both)
         offset + len <= address_regions[a].base + address_regions[a].length) ||
        (offset <= address_regions[a].base && //Or, we're entirely contained within it
         offset + len >= address_regions[a].base + address_regions[a].length))
    {
      uint32_t uselen = len;
      uint32_t usepos;
      uint32_t usetarget;

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

static int xmp_write(const void *buf, uint32_t len, uint64_t offset, void *userdata)
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

  if (offset > (uint64_t)aop.blksize * aop.size_blocks)
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

static int xmp_trim(uint64_t from, uint32_t len, void *userdata)
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
        printf("%s/%s  %ld\n", path, dir->d_name, st.st_size);
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
            "  %s /dev/nbd0 ./folder_to_export [--debug]\n"
            "Don't forget to load the nbd kernel module (`modprobe nbd`) and\n"
            "run as root. Adding --debug will turn on debugging\n",
            argv[0]);
    return 1;
  }

  //Check the debug flag
  if (argc > 3)
  {
    if (strcmp(argv[3], "--debug") == 0)
    {
      xmpl_debug = 1;
    }
  }

  //Setup the virtual disk
  build_mbr();
  //Update the disksize based on the boot sector configuration
  uint32_t DiskSize = build_boot_sector(&bootentry, xmpl_debug);
  aop.size_blocks = DiskSize;

  build_fats();
  build_root_dir();
  //Populate the virtual disk with the contents of the given FS
  scan_folder(argv[2]);

  fprintf(stderr, "Scan complete, launching block device\n");

  return buse_main(argv[1], &aop, (void *)&xmpl_debug);
}
