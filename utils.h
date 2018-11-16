#include "vsfat.h"

int8_t arrays_equal(uint8_t *left, uint8_t *right, int8_t length);
uint32_t ceil_div(uint32_t x, uint32_t y);
int8_t file_exists(Fat_Directory *current_dir, uint8_t filename[8], uint8_t extension[3]);
int updateSFN(uint8_t *filename, int *tildePos, int iterator);
void format_name_83(Fat_Directory *current_dir, unsigned char *input, uint32_t length, unsigned char *filename,
                    unsigned char *ext, unsigned char *lfn, unsigned int *lfnlength);
void printBootSect(struct BootEntry *bootentry);