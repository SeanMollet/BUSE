/*
 * vsfat - virtual synthetic FAT filesystem on network block device from local folder
 * Copyright (C) 2017 Sean Mollet
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

#include "utils.h"

int32_t min(int32_t left, int32_t right)
{
  if (left < right)
  {
    return left;
  }
  return right;
}
//Compare two arrays up to "length" bytes for equality
int8_t arrays_equal(uint8_t *left, uint8_t *right, int8_t length)
{
  for (length--; length >= 0; length--)
  {
    if (left[length] != right[length])
    {
      return 0;
    }
  }
  return 1;
}

//Utility function for division that always returns a rounded up value
uint32_t ceil_div(uint32_t x, uint32_t y)
{
  if (x == 0)
  {
    return 0;
  }
  return (x % y) ? x / y + 1 : x / y; //Ceiling division
}

//Check if a file exists in the given directory
int8_t file_exists(Fat_Directory *current_dir, uint8_t *filename, uint8_t *extension)
{
  FileEntry *testFile = current_dir->files;
  while (testFile != 0)
  {
    if (arrays_equal(testFile->Filename, filename, 8) && arrays_equal(testFile->Ext, extension, 3))
    {
      return 1;
    }
    testFile = testFile->next;
  }

  return 0;
}

//Add the ~ and the number (given in iterator) to the given filename
int updateSFN(uint8_t *filename, int *tildePos, int iterator)
{
  //First time with a two digit iterator, so we move the tilde
  if (iterator == 10)
  {
    //If the tildePos is lower than this, there's no need to move it
    if (*tildePos > 5)
    {
      (*tildePos)--;
    }
  }
  filename[*tildePos] = '~';
  if (iterator > 10)
  {
    int tensPlace = iterator / 10;
    filename[*tildePos + 1] = tensPlace + 48; //Convert int to ascii number
    int onesPlace = iterator % 10;
    filename[*tildePos + 2] = onesPlace + 48; //Convert into to ascii number

    if (iterator > 99)
    {
      fprintf(stderr, "Too many file collisions for %s\n", filename);
      return -1;
    }
  }
  else
  {
    filename[*tildePos + 1] = iterator + 48;
  }
  return 0;
}

//Convert filename to FAT32 8.3 format and generate an lfn string in proper format
void format_name_83(Fat_Directory *current_dir, unsigned char *input, uint32_t length, unsigned char *filename,
                    unsigned char *ext, unsigned char *lfn, unsigned int *lfnlength)
{
  unsigned char *adjusted = malloc(length);

  //If none of the lfn_required things exist in this filename, we don't build one
  int8_t lfn_required = 0;
  int32_t period = -1;

  *lfnlength = 0;

  //First we take out things we know don't belong
  for (int a = 0; a < (int32_t)length; a++)
  {
    adjusted[a] = input[a];
    if (input[a] == 0x20)
    {                     // Space
      adjusted[a] = 0x5F; // _
      lfn_required = 1;
    }
    if (input[a] == 0xe5)
    {
      adjusted[a] = 0x05;
      lfn_required = 1;
    }
    unsigned char test = toupper(input[a]); //This does nothing if there isn't an upper case form
    if (test != input[a])
    {
      adjusted[a] = test;
      lfn_required = 1;
    }
    //Check if this is a period
    if (input[a] == 46)
    {
      period = a;
    }
  }

  //Finally, check if the length exceeds the allowed amount
  if (length > 11                   //Overall length is too long
      || (length > 8 && period < 0) //Length is too long without an extension
      || (length - period) > 4      //The extension is longer than 3 characters
  )
  {
    lfn_required = 1;
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
      ext[a] = adjusted[period + a + 1];
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
      filename[a] = adjusted[a];
    }
    else
    {
      filename[a] = 0x20;
    }
  }

  if (lfn_required)
  {
    int iterator = 1; //Ascii 1
    int tildePos = 6;
    //If we have a file that requires LFN, but is shorter than 8
    if (period == 7)
    {
      tildePos = 6;
    }
    if (period < 7)
    {
      tildePos = period;
    }

    //Test until we find a usable name
    do
    {
      updateSFN(filename, &tildePos, iterator);
      iterator++;
    } while (file_exists(current_dir, filename, ext));

    //We have a usable name, so now we copy the given name into the lfn field, padding with 0x00 to fake UCS-2.
    //TODO: Properly support UTF-16/UCS-2
    for (uint8_t i = 0; i < length; i++)
    {
      lfn[(i * 2)] = input[i];
      lfn[(i * 2) + 1] = 0x00;
      (*lfnlength)++;
    }
  }
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