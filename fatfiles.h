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

#include <ctype.h>

void fat_find_free();
int fat_new_file(uint32_t file_fat_position, unsigned char *data, char *filepath, uint32_t length);
int dir_entry_sectors_needed(uint32_t entries_needed);
int dir_add_entry(unsigned char *entry, uint32_t length);
void up_dir();
void add_file(char *name, char *filepath, uint32_t size, u_char isDirectory);
