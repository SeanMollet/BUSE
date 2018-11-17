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
#include <stdint.h>

#include "vsfat.h"
#include "utils.h"

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        BootEntry entry;
        FILE *fd = fopen(argv[1], "rb");
        if (fd)
        {
            fseek(fd, 0x100000, SEEK_SET);
            size_t read_count = fread((unsigned char *)&entry, 512, 1, fd);
            fclose(fd);
            printBootSect(&entry);
        }
    }
    else
    {
        printf("Usage: %s <file to scan>\n", argv[0]);
    }
}