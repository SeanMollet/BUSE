#ifndef VSFAT_H_INCLUDED
#define VSFAT_H_INCLUDED

#pragma pack(push,1)	/* BYTE align in memory (no padding) */
typedef struct BootEntry{
	u_int8_t BS_jmpBoot[3];	/* Assembly instruction to jump to boot code */
	u_int8_t BS_OEMName[8]; 	/* OEM Name in ASCII */
	u_int16_t BPB_BytsPerSec; 	/* Bytes per sector. Allowed values include 512,1024, 2048, and 4096 */
	u_int8_t BPB_SecPerClus; 	/* Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB  or smaller */
	u_int16_t BPB_RsvdSecCnt;	/* Size in sectors of the reserved area */
	u_int8_t BPB_NumFATs; 	/* Number of FATs */
	u_int16_t BPB_RootEntCnt;	/* Maximum number of files in the root directory for  FAT12 and FAT16. This is 0 for FAT32 */
	u_int16_t BPB_TotSec16; 	/* 16-bit value of number of sectors in file system */
	u_int8_t BPB_Media; 	/* Media type */
	u_int16_t BPB_FATSz16;	/* 16-bit size in sectors of each FAT for FAT12 and  FAT16. For FAT32, this field is 0 */
	u_int16_t BPB_SecPerTrk; 	/* Sectors per track of storage device */
	u_int16_t BPB_NumHeads;	/* Number of heads in storage device */
	u_int32_t BPB_HiddSec;	/* Number of sectors before the start of partition */
	u_int32_t BPB_TotSec32;	/* 32-bit value of number of sectors in file system.  Either this value or the 16-bit value above must be  0 */
	u_int32_t BPB_FATSz32;	/* 32-bit size in sectors of one FAT */
	u_int16_t BPB_ExtFlags;	/* A flag for FAT */
	u_int16_t BPB_FSVer;	/* The major and minor version number */
	u_int32_t BPB_RootClus;	/* Cluster where the root directory can be found */
	u_int16_t BPB_FSInfo;	/* Sector where FSINFO structure can be found */
	u_int16_t BPB_BkBootSec;	/* Sector where backup copy of boot sector is located */
	u_int8_t BPB_Reserved[12];	/* Reserved */
	u_int8_t BS_DrvNum;	/* BIOS INT13h drive number */
	u_int8_t BS_Reserved1;	/* Not used */
	u_int8_t BS_BootSig;	/* Extended boot signature to identify if the next three values are valid */
	u_int32_t BS_VolID;		/* Volume serial number */	
	u_int8_t BS_VolLab[11];	/* Volume label in ASCII. User defines when creating the file system */
	u_int8_t BS_FilSysType[8];	/* File system type label in ASCII */ 
	u_int8_t BS_BootCode32[420]; /* Boot code, leave this empty */
	u_int16_t BS_BootSign; /* Needs to contain 0xAA55 */
}BootEntry;
#pragma pack(pop) 

#endif /* VSFAT_H_INCLUDED */

