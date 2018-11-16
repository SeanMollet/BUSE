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

static unsigned char *mbr;

BootEntry bootentry;
uint32_t *fat = 0;
uint32_t current_fat_position; // 0 and 1 are special and 2 is the root dir
Fat_Directory root_dir;
Fat_Directory *current_dir;

static unsigned const char fat_end[] = {0xFF, 0xFF, 0xFF, 0xFF};
static int xmpl_debug = 1;

static int xmp_read(void *buf, uint32_t len, uint64_t offset,
                    void *userdata);
static int xmp_write(const void *buf, uint32_t len, uint64_t offset,
                     void *userdata);
static void xmp_disc(void *userdata);
static int xmp_flush(void *userdata);
static int xmp_trim(uint64_t from, uint32_t len, void *userdata);

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
static int fat_new_file(uint32_t file_fat_position, unsigned char *data, char *filepath, uint32_t length)
{
  //length always has to be at least 1
  if (length <= 0)
  {
    length = 1;
  }

  //Make sure we have enough space
  uint32_t cluster_size = (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus);
  uint32_t clusters_required = ceil_div(length, cluster_size); //Ceiling division
  if (clusters_required >
      ((bootentry.BPB_FATSz32 * bootentry.BPB_BytsPerSec) / 4) - file_fat_position)
  { //Free clusters
    return -1;
  }
  add_address_region(address_from_fatclus(file_fat_position), length, data, filepath);

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
    fat[file_fat_position] = file_fat_position + 1;
    file_fat_position++;
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

  memcpy(&fat[file_fat_position], fat_end, sizeof(fat_end)); // Terminate this chain
  return 0;
}

//Check if the current directory entry has space available
static int dir_entry_sectors_needed(uint32_t entries_needed)
{
  if (entries_needed == 0)
  {
    entries_needed = 1;
  }

  uint32_t cluster_size = (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus);
  uint32_t entrys_per_cluster = cluster_size / sizeof(DirEntry);

  if (current_dir->dirtables == 0)
  {
    return ceil_div(entries_needed, entrys_per_cluster);
  }
  uint32_t free_entries = entrys_per_cluster - (current_dir->current_dir_position % entrys_per_cluster);
  if (free_entries > entries_needed)
  {
    return 0;
  }
  //Subtract the last of what we already have
  entries_needed -= free_entries;
  return ceil_div(entries_needed, entrys_per_cluster);
}

//This does accept multiple entries. However, it does not accept more than a single file worth
static int dir_add_entry(unsigned char *entry, uint32_t length)
{
  while (length > 0)
  {
    uint32_t cluster_size = (bootentry.BPB_BytsPerSec * bootentry.BPB_SecPerClus);
    uint32_t entrys_per_cluster = cluster_size / sizeof(DirEntry);
    uint32_t current_cluster_free = current_dir->current_dir_position % entrys_per_cluster;

    //Make sure we have an initial entry
    if (current_dir->dirtables == 0)
    {
      current_dir->dirtables = malloc(sizeof(DirTable));
      current_dir->dirtables->next = 0;

      current_dir->dirtables->dirtable = malloc(cluster_size);
      memset(current_dir->dirtables->dirtable, 0, cluster_size);

      uint64_t dest = address_from_fatclus(current_dir->dir_location);
      add_address_region(dest, cluster_size, current_dir->dirtables->dirtable, 0);
      current_cluster_free = entrys_per_cluster;

      memcpy(&fat[current_dir->dir_location], fat_end, sizeof(fat_end)); // Terminate this chain in the FAT
    }

    //Make sure we don't exceed the 2Mb limit for directory size
    if (current_dir->current_dir_position + 1 > (1024 * 1024 * 2) / sizeof(DirEntry))
    {
      return -1;
    }

    //Add another cluster if needed
    if (current_cluster_free < 1)
    {
      fat_find_free();

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
      memset(final_dir_table->dirtable, 0, cluster_size);
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
    uint32_t final_table_pos = current_dir->current_dir_position;
    while (final_dir_table->next != 0)
    {
      final_dir_table = final_dir_table->next;
      final_table_pos -= entrys_per_cluster;
    }

    //copy the data in
    memcpy(final_dir_table->dirtable + final_table_pos * sizeof(DirEntry), entry, sizeof(DirEntry));
    current_dir->current_dir_position++;
    //Move to the next one
    length--;
    entry += sizeof(DirEntry);
  }
  //Profit!
  return 0;
}

static void up_dir()
{
  //Free the current dir (we shouldn't be leaving until we're done with it)
  //Commented out for debugging - will need to be removed for tracking if writing is desired
  if (current_dir != &root_dir)
  {
    //I purposely don't free dirtables, because it's mapped
    free(current_dir->path);
    free(current_dir);

    //Clear the filename entries as well
    FileEntry *freeFile = current_dir->files;
    while (freeFile != 0)
    {
      FileEntry *next = freeFile->next;
      free(freeFile);
      freeFile = next;
    }
  }

  //If the parent is root, we just stay at the root
  current_dir = current_dir->parent;
}

//Add a file to the mapping space
static void add_file(char *name, char *filepath, uint32_t size, u_char isDirectory)
{
  DirEntry entry;
  unsigned char *lfn, *lfn_original;
  unsigned int lfnlength = 0;
  lfn = malloc(LFN_Max_length);
  lfn_original = lfn;

  //Make sure it's clear
  memset(&entry, 0, sizeof(DirEntry));

  //For now, just stupid 8.3
  //Format uses unsigned char so it can potentially handle UTF-16 in the future
  //We don't currently get 7 bit ascii input, so this cast clears the warning
  //and will work correctly with the input we're given
  format_name_83(current_dir, (unsigned char *)name, strlen(name), entry.DIR_Name, entry.DIR_Ext, lfn, &lfnlength);

  //Add this filename to the 8.3 linked list to prevent colisions on LFNs
  //If this directory is empty
  if (current_dir->files == 0)
  {
    current_dir->files = malloc(sizeof(FileEntry));
    current_dir->files->next = 0;
  }

  //Find the last entry (which is a new entry)
  FileEntry *newFile = current_dir->files;
  while (newFile->next != 0)
  {
    newFile = newFile->next;
  }
  newFile->next = malloc(sizeof(FileEntry));
  memset(newFile->next, 0, sizeof(FileEntry));
  memcpy(newFile->Filename, entry.DIR_Name, 8);
  memcpy(newFile->Ext, entry.DIR_Ext, 3);

  //Figure out how many lfn directory entries we'll need
  int lfnEntryCount = ceil_div(lfnlength, LFN_Chars_Per_Entry);
  if (lfnEntryCount > LFN_Max_Entries)
  {
    lfnEntryCount = LFN_Max_Entries;
  }

  //No point adding entries if we don't have an LFN
  if (lfnlength > 0)
  {
    //Build the LFN directory entries
    LfnEntry *lfnEntries = malloc(sizeof(LfnEntry) * lfnEntryCount);
    memset(lfnEntries, 0xFF, sizeof(LfnEntry) * lfnEntryCount);

    for (int entryCount = 0; entryCount < lfnEntryCount; entryCount++)
    {
      int currentEntry = lfnEntryCount - entryCount - 1;
      //Set the flag on the last entry
      if (currentEntry == 0)
      {
        lfnEntries[currentEntry].LFN_Seq = ((entryCount + 1) & LFN_Seq_Mask) | LFN_First_Flag;
      }
      else
      {
        lfnEntries[currentEntry].LFN_Seq = (entryCount + 1) & LFN_Seq_Mask;
      }
      int chars = min(lfnlength, 5);
      memset(&lfnEntries[currentEntry].LFN_Name, 0, 10);
      memcpy(&lfnEntries[currentEntry].LFN_Name, lfn, chars * 2);
      lfnlength -= chars;
      lfn += chars * 2;

      lfnEntries[currentEntry].LFN_Attributes = LFN_Attr;
      lfnEntries[currentEntry].LFN_Type = 0x00;
      lfnEntries[currentEntry].LFN_Checksum = fn_checksum(entry.DIR_Name, entry.DIR_Ext);

      chars = min(lfnlength, 6);
      memset(&lfnEntries[currentEntry].LFN_Name2, 0, 12);
      memcpy(&lfnEntries[currentEntry].LFN_Name2, lfn, chars * 2);
      lfnlength -= chars;
      lfn += chars * 2;

      lfnEntries[currentEntry].LFN_Cluster_HI = 0x00;
      lfnEntries[currentEntry].LFN_Cluster_LO = 0x00;

      chars = min(lfnlength, 2);
      memset(&lfnEntries[currentEntry].LFN_Name3, 0, 4);
      memcpy(&lfnEntries[currentEntry].LFN_Name3, lfn, chars * 2);
      lfnlength -= chars;
      lfn += chars * 2;
    }

    dir_add_entry((unsigned char *)lfnEntries, lfnEntryCount);
    free(lfnEntries);
  }

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

  //If the directory entry for this file will require a cluster
  //Then our file will end up in the next cluster after that
  //So, we need to increase this now to generate the correct entry
  uint32_t filePosition = current_fat_position;
  uint32_t clusters_needed = dir_entry_sectors_needed(lfnEntryCount);
  filePosition += clusters_needed;

  entry.DIR_FstClusLO = (uint16_t)(filePosition & 0xFFFF);
  entry.DIR_FstClusHI = (uint16_t)((filePosition & 0xFFFF0000) >> 16);
  entry.DIR_FileSize = size;

  //dir_add_entry wants a byte array so it can have multiple entries chained together
  //We just cast the struct pointer and tell it how many there are (1 for short filename)

  if (dir_add_entry((unsigned char *)&entry, 1) == 0)
  {
    if (!isDirectory)
    {
      //Allocate the array for the actual data
      fat_new_file(filePosition, 0, filepath, size);
    }
    else
    {
      //Make a new directory entry and change to it
      Fat_Directory *new_dir = malloc(sizeof(Fat_Directory));
      new_dir->path = filepath;
      new_dir->dirtables = 0;
      new_dir->current_dir_position = 0;
      new_dir->parent = current_dir;
      new_dir->dir_location = filePosition;
      new_dir->files = 0;
      current_dir = new_dir;

      //Add the . and .. entries to this dir

      DirEntry dotentry;
      //Adding the . entry automatically maps the memory space for the first cluster of this
      //dir entry
      // .
      memset(&dotentry, 0, sizeof(DirEntry));
      dotentry.DIR_Attr = DIR_Attr_Archive | DIR_Attr_Directory;
      dotentry.DIR_Name[0] = '.';
      dotentry.DIR_FstClusLO = (uint16_t)(current_dir->dir_location & 0x00FF);
      dotentry.DIR_FstClusHI = (uint16_t)(current_dir->dir_location & 0xFF00) >> 16;
      dir_add_entry((unsigned char *)&dotentry, 1);

      //..
      dotentry.DIR_Name[1] = '.';
      dotentry.DIR_FstClusLO = (uint16_t)(current_dir->parent->dir_location & 0x00FF);
      dotentry.DIR_FstClusHI = (uint16_t)(current_dir->parent->dir_location & 0xFF00) >> 16;
      dir_add_entry((unsigned char *)&dotentry, 1);
    }
  }
  free(lfn_original);
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

  build_mbr(mbr);
  build_boot_sector(&bootentry, xmpl_debug);
  build_fats();
  build_root_dir();
  scan_folder(argv[2]);

  return buse_main(argv[1], &aop, (void *)&xmpl_debug);
}
