/*
 * busexmp - example memory-based block device using BUSE
 * Copyright (C) 2013 Adam Cozzette
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

#include "buse.h"
#include "vsfat.h"

static unsigned char *mbr;
static const u_int32_t part1_base = 1048576;

static BootEntry bootentry;

static unsigned char *fat;

static AddressRegion *address_regions;
static u_int32_t address_regions_count;

static int xmpl_debug = 1;



static int xmp_read(void *buf, u_int32_t len, u_int64_t offset, void *userdata);
static int xmp_write(const void *buf, u_int32_t len, u_int64_t offset, void *userdata);
static void xmp_disc(void *userdata);
static int xmp_flush(void *userdata);
static int xmp_trim(u_int64_t from, u_int32_t len, void *userdata);
static void add_address_region(u_int64_t base, u_int64_t length,void *mem_pointer,char *file_path);
static void printBootSect(struct BootEntry bootentry);


static struct buse_operations aop = {
  .read = xmp_read,
  .write = xmp_write,
  .disc = xmp_disc,
  .flush = xmp_flush,
  .trim = xmp_trim,
  .blksize = 512,
  .size_blocks = 4292870144,
};

//static u_int64_t part_blocks = aop.size_blocks-2048;


static int xmp_read(void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  if (*(int *)userdata)
    fprintf(stderr, "R - %llu, %u\n", offset, len);

  //We make the gross assumption here that anything after a mapped region
  //is full of zereos. Since we're controlling the FS, that should continue 
  //to be true. But, a bug might cause it to not be true and would be
  //extremely hard to track down
  
  //Check if this read falls within a mapped area
  for(u_int32_t a=0;a<address_regions_count;a++){
    if(offset >= address_regions[a].base &&
       offset <= address_regions[a].base+address_regions[a].length){
      //For real memory mapped stuff
      if(address_regions[a].mem_pointer){
        //Make sure the buffer is zeroed
        memset(buf,0,len);
        u_int32_t uselen = len;
        u_int64_t usepos = offset - address_regions[a].base;
      
        if(usepos + len > address_regions[a].length){
          uselen = address_regions[a].length - usepos;
        }
        memcpy(buf,address_regions[a].mem_pointer,uselen);
        return 0;
      }
      //A mapped in file
      if(address_regions[a].file_path){
        //TODO: Implement me
      }
    }
  }
  
  //memcpy(buf, (char *)data + offset, len);
  return 0;
}

static int xmp_write(const void *buf, u_int32_t len, u_int64_t offset, void *userdata)
{
  buf;
  if (*(int *)userdata)
    fprintf(stderr, "W - %llu, %u\n", offset, len);
  
  if(offset>(u_int64_t) aop.blksize* aop.size_blocks){
    return 0;
    }
  //memcpy((char *)data + offset, buf, len);
  return 0;
}

static void xmp_disc(void *userdata)
{
  (void)(userdata);
  fprintf(stderr, "Received a disconnect request.\n");
}

static int xmp_flush(void *userdata)
{
  (void)(userdata);
  fprintf(stderr, "Received a flush request.\n");
  return 0;
}

static int xmp_trim(u_int64_t from, u_int32_t len, void *userdata)
{
  (void)(userdata);
  fprintf(stderr, "T - %llu, %u\n", from, len);
  return 0;
}

static void add_address_region(u_int64_t base, u_int64_t length,void *mem_pointer,char *file_path){
  address_regions_count++;
  address_regions = realloc(address_regions,address_regions_count*sizeof(AddressRegion));
  address_regions[address_regions_count-1].base = base;
  address_regions[address_regions_count-1].length = length;
  address_regions[address_regions_count-1].mem_pointer = mem_pointer;
  address_regions[address_regions_count-1].file_path = file_path;
}

static void build_mbr()
{
  unsigned char bootcode[] = {0xFA,0xB8,0x00,0x10,0x8E,0xD0,0xBC,0x00,0xB0,0xB8,0x00,0x00,0x8E,0xD8,0x8E,0xC0,0xFB,0xBE,0x00,0x7C,0xBF,0x00,0x06,0xB9,0x00,0x02,0xF3,0xA4,0xEA,0x21,0x06,0x00,0x00,0xBE,0xBE,0x07,0x38,0x04,0x75,0x0B,0x83,0xC6,0x10,0x81,0xFE,0xFE,0x07,0x75,0xF3,0xEB,0x16,0xB4,0x02,0xB0,0x01,0xBB,0x00,0x7C,0xB2,0x80,0x8A,0x74,0x01,0x8B,0x4C,0x02,0xCD,0x13,0xEA,0x00,0x7C,0x00,0x00,0xEB,0xFE,0x00};
  unsigned char serial[] = {0xDE,0xAB,0xBE,0xEF};
  unsigned char parts[4][16] = {{0x00,0x20,0x21,0x00,0x0c,0xcd,0xfb,0xd2,0x00,0x08,0x00,0x00,0x00,0xf8,0xdf,0xff},
                             {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
                             {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
                             {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}};
  unsigned char footer[] = {0x55,0xAA};

  mbr = malloc(512);
  memset(mbr,0,512);
  
  memcpy(mbr,&bootcode,sizeof(bootcode));
  memcpy(mbr+440,&serial,sizeof(serial));
  
  for(int a=0;a<4;a++){
    memcpy(mbr+446+16*a,&parts[a],16);
  }
  memcpy(mbr+510,&footer,sizeof(footer));
  add_address_region(0,512,mbr,0);
  
}

static void build_boot_sector()
{

  //Build our bootsector
  bootentry.BS_jmpBoot[0]=0xeb;
  bootentry.BS_jmpBoot[1]=0x58;
  bootentry.BS_jmpBoot[2]=0x90;
  strncpy((char*) &bootentry.BS_OEMName,"MSDOS5.0",8);
  bootentry.BPB_BytsPerSec=512;
  bootentry.BPB_SecPerClus=1;
  bootentry.BPB_RsvdSecCnt=32;
  bootentry.BPB_NumFATs=2;
  bootentry.BPB_RootEntCnt=0;
  bootentry.BPB_TotSec16=0;
  bootentry.BPB_Media=248;
  bootentry.BPB_FATSz16=0;
  bootentry.BPB_SecPerTrk=32;
  bootentry.BPB_NumHeads=64;
  bootentry.BPB_HiddSec=0;
  bootentry.BPB_TotSec32=102400;
  bootentry.BPB_FATSz32=788;
  bootentry.BPB_ExtFlags=0;
  bootentry.BPB_FSVer=0;
  bootentry.BPB_RootClus=2;
  bootentry.BPB_FSInfo=1;
  bootentry.BPB_BkBootSec=6;
  bootentry.BS_DrvNum=128;
  bootentry.BS_Reserved1=0;
  bootentry.BS_BootSig=29;
  bootentry.BS_VolID=0x8456f237;
  bootentry.BS_BootSign = 0xAA55;
    
  unsigned char vol[] = {0x56,0x53,0x46,0x41,0x54,0x46,0x53,0x20,0x20,0x20,0x20};
  unsigned char fstype[] = {0x46,0x41,0x54,0x33,0x32,0x20,0x20,0x20};
  
  memcpy(&bootentry.BS_VolLab,vol,sizeof(vol));
  memcpy(&bootentry.BS_FilSysType,fstype,sizeof(fstype));
  if(xmpl_debug){    
    printBootSect(bootentry);
  }
  
  add_address_region(part1_base,512,&bootentry,0);
  
}

static u_int64_t address_from_fatsec(u_int32_t fatsec)
{
  return (u_int64_t) part1_base + (u_int64_t) bootentry.BPB_BytsPerSec*fatsec;
}

static u_int32_t fat_location(u_int32_t fatnum)
{
  return bootentry.BPB_RsvdSecCnt + bootentry.BPB_FATSz32*fatnum;
}

static void build_fats()
{
  unsigned char fatdata[] = {0xF8,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xF8,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0x0F};
  fat = malloc(sizeof(fatdata));
  memcpy(fat,fatdata,sizeof(fatdata));
  
  //For now, just map these in blind
  printf("fat0: %llu\n",address_from_fatsec(fat_location(0)));
  printf("fat1: %llu\n",address_from_fatsec(fat_location(1)));
  
  add_address_region(address_from_fatsec(fat_location(0)),sizeof(fatdata),fat,0);
  add_address_region(address_from_fatsec(fat_location(1)),sizeof(fatdata),&fat,0);    
}


int main(int argc, char *argv[])
{ 
  if (argc != 2)
  {
    fprintf(stderr, 
        "Usage:\n"
        "  %s /dev/nbd0\n"
        "Don't forget to load nbd kernel module (`modprobe nbd`) and\n"
        "run example from root.\n", argv[0]);
    return 1;
  }
  
//  fprintf(stderr,"Creating virtual disk of size %llu\n",aop.size);

//  data = malloc(aop.size);

  build_mbr(); 
  build_boot_sector();
  build_fats();
  
  return buse_main(argv[1], &aop, (void *)&xmpl_debug);
}


static void printBootSect(BootEntry bootentry)
{
fprintf(stderr,"BS_jmpBoot[3] %02x %02x %02x\n",
bootentry.BS_jmpBoot[0],
bootentry.BS_jmpBoot[1],
bootentry.BS_jmpBoot[2]);

fprintf(stderr,"BS_OEMName[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
bootentry.BS_OEMName[0],
bootentry.BS_OEMName[1],
bootentry.BS_OEMName[2],
bootentry.BS_OEMName[3],
bootentry.BS_OEMName[4],
bootentry.BS_OEMName[5],
bootentry.BS_OEMName[6],
bootentry.BS_OEMName[7],
bootentry.BS_OEMName);

fprintf(stderr,"BPB_BytsPerSec=%u\n",bootentry.BPB_BytsPerSec);
fprintf(stderr,"BPB_SecPerClus=%u\n",bootentry.BPB_SecPerClus);
fprintf(stderr,"BPB_RsvdSecCnt=%u\n",bootentry.BPB_RsvdSecCnt);
fprintf(stderr,"BPB_NumFATs=%u\n",bootentry.BPB_NumFATs);
fprintf(stderr,"BPB_RootEntCnt=%u\n",bootentry.BPB_RootEntCnt);
fprintf(stderr,"BPB_TotSec16=%u\n",bootentry.BPB_TotSec16);
fprintf(stderr,"BPB_Media=%u\n",bootentry.BPB_Media);
fprintf(stderr,"BPB_FATSz16=%u\n",bootentry.BPB_FATSz16);
fprintf(stderr,"BPB_SecPerTrk=%u\n",bootentry.BPB_SecPerTrk);
fprintf(stderr,"BPB_NumHeads=%u\n",bootentry.BPB_NumHeads);
fprintf(stderr,"BPB_HiddSec=%u\n",bootentry.BPB_HiddSec);
fprintf(stderr,"BPB_TotSec32=%u\n",bootentry.BPB_TotSec32);
fprintf(stderr,"BPB_FATSz32=%u\n",bootentry.BPB_FATSz32);
fprintf(stderr,"BPB_ExtFlags=%u\n",bootentry.BPB_ExtFlags);
fprintf(stderr,"BPB_FSVer=%u\n",bootentry.BPB_FSVer);
fprintf(stderr,"BPB_RootClus=%u\n",bootentry.BPB_RootClus);
fprintf(stderr,"BPB_FSInfo=%u\n",bootentry.BPB_FSInfo);
fprintf(stderr,"BPB_BkBootSec=%u\n",bootentry.BPB_BkBootSec);
fprintf(stderr,"BPB_Reserved[12] %02x\n",bootentry.BPB_Reserved[12]);
fprintf(stderr,"BS_DrvNum=%u\n",bootentry.BS_DrvNum);
fprintf(stderr,"BS_Reserved1=%02x\n",bootentry.BS_Reserved1);
fprintf(stderr,"BS_BootSig=%02x\n",bootentry.BS_BootSig);
fprintf(stderr,"BS_VolID=%02x\n",bootentry.BS_VolID);
fprintf(stderr,"BS_VolLab[11] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %.11s\n",
bootentry.BS_VolLab[0],
bootentry.BS_VolLab[1],
bootentry.BS_VolLab[2],
bootentry.BS_VolLab[3],
bootentry.BS_VolLab[4],
bootentry.BS_VolLab[5],
bootentry.BS_VolLab[6],
bootentry.BS_VolLab[7],
bootentry.BS_VolLab[8],
bootentry.BS_VolLab[9],
bootentry.BS_VolLab[10],
bootentry.BS_VolLab);

fprintf(stderr,"BS_FilSysType[8] %02x %02x %02x %02x %02x %02x %02x %02x %.8s\n",
bootentry.BS_FilSysType[0],
bootentry.BS_FilSysType[1],
bootentry.BS_FilSysType[2],
bootentry.BS_FilSysType[3],
bootentry.BS_FilSysType[4],
bootentry.BS_FilSysType[5],
bootentry.BS_FilSysType[6],
bootentry.BS_FilSysType[7],
bootentry.BS_FilSysType);

fprintf(stderr,"\n");
}