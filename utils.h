#include "vsfat.h"

int8_t arrays_equal(uint8_t *left, uint8_t *right, int8_t length);
uint32_t ceil_div(uint32_t x, uint32_t y);
void printBootSect(struct BootEntry *bootentry);