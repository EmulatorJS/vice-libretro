/*
 * plus4embedded.c - Code for embedding plus4 data files.
 *
 * This feature is only active when --enable-embedded is given to the
 * configure script, its main use is to make developing new ports easier
 * and to allow ports for platforms which don't have a filesystem, or a
 * filesystem which is hard/impossible to load data files from.
 *
 * Written by
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#ifdef USE_EMBEDDED
#include <string.h>
#include <stdio.h>

#include "embedded.h"
#include "machine.h"
#include "plus4mem.h"
#include "plus4rom.h"
#include "plus4memrom.h"

#include "ted_yape_pal_vpl.h"
#include "ted_yape_ntsc_vpl.h"
#include "ted_colodore_ted_vpl.h"

#include "plus4kernal005.h"
#include "plus4kernal232.h"
#include "plus4kernal364.h"
#include "plus4c2lo364.h"

static embedded_t plus4files[] = {
    { PLUS4_BASIC_NAME, PLUS4_BASIC_ROM_SIZE, PLUS4_BASIC_ROM_SIZE, PLUS4_BASIC_ROM_SIZE, plus4memrom_basic_rom },
    { PLUS4_KERNAL_PAL_REV5_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, plus4memrom_kernal_rom },
    { PLUS4_3PLUS1LO_NAME, PLUS4_BASIC_ROM_SIZE, PLUS4_BASIC_ROM_SIZE, PLUS4_BASIC_ROM_SIZE, extromlo1 },
    { PLUS4_3PLUS1HI_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, extromhi1 },
    { PLUS4_KERNAL_NTSC_REV5_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, plus4memrom_kernal005_rom },
    { PLUS4_KERNAL_NTSC_REV1_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, plus4memrom_kernal232_rom },
    { PLUS4_KERNAL_NTSC_364_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, plus4memrom_kernal364_rom },
    { PLUS4_C2LO_NAME, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, PLUS4_KERNAL_ROM_SIZE, plus4memrom_c2lo364_rom },
    EMBEDDED_LIST_END
};

static embedded_palette_t palette_files[] = {
    { "yape-pal", "yape-pal.vpl", 128, ted_yape_pal_vpl },
    { "yape-ntsc", "yape-ntsc.vpl", 128, ted_yape_ntsc_vpl },
    { "colodore_ted", "colodore_ted.vpl", 128, ted_colodore_ted_vpl },
    EMBEDDED_PALETTE_LIST_END
};

static size_t embedded_match_file(const char *name, unsigned char *dest, int minsize, int maxsize, embedded_t *emb)
{
    int i = 0;

    while (emb[i].name != NULL) {
        if (!strcmp(name, emb[i].name) && minsize == emb[i].minsize && maxsize == emb[i].maxsize) {
            if (emb[i].esrc != NULL) {
                if (emb[i].size != minsize) {
                    memcpy(dest, emb[i].esrc, maxsize);
                } else {
                    memcpy(dest + maxsize - minsize, emb[i].esrc, minsize);
                }
            }
            return emb[i].size;
        }
        i++;
    }
    return 0;
}

size_t embedded_check_file(const char *name, unsigned char *dest, int minsize, int maxsize)
{
    size_t retval;

    if ((retval = embedded_check_extra(name, dest, minsize, maxsize)) != 0) {
        return retval;
    }

    if ((retval = embedded_match_file(name, dest, minsize, maxsize, plus4files)) != 0) {
        return retval;
    }
    return 0;
}

int embedded_palette_load(const char *fname, palette_t *p)
{
    int i = 0;
    int j;
    unsigned char *entries;

    while (palette_files[i].name1 != NULL) {
        if (!strcmp(palette_files[i].name1, fname) || !strcmp(palette_files[i].name2, fname)) {
            entries = palette_files[i].palette;
            for (j = 0; j < palette_files[i].num_entries; j++) {
                p->entries[j].red = entries[(j * 4) + 0];
                p->entries[j].green = entries[(j * 4) + 1];
                p->entries[j].blue = entries[(j * 4) + 2];
            }
            return 0;
        }
        i++;
    }
    return -1;
}
#endif
