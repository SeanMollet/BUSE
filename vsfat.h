#ifndef VSFAT_H_INCLUDED
#define VSFAT_H_INCLUDED

#include <linux/limits.h>

//Ultimately, the file_path might move out of here
//But, for now, this is a very straight forward way to handle the mapping

typedef struct AddressRegion{
	u_int64_t base;
	u_int64_t length;
	void *mem_pointer;
	char *file_path;
}AddressRegion;


#pragma pack(push,1)	/* BYTE align in memory (no padding) */
typedef struct BootEntry{
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
}BootEntry;

typedef struct DirEntry {
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
}DirEntry;
#pragma pack(pop) 

#endif /* VSFAT_H_INCLUDED */

