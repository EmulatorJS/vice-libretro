
/*
 * cbm2-stubs.c - dummies for unneeded/unused functions
 *
 * Written by
 *  groepaz <groepaz@gmx.net>
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

#include <stdlib.h>
#include <stdbool.h>

#include "c64/cart/clockport.h"
#include "cartridge.h"
#include "mididrv.h"
#include "pet/petpia.h"
#ifdef HAVE_LIBCURL
#include "userport_wic64.h"
#endif
#include "rsuser.h"
#include "c64/c64parallel.h"    /* FIXME: use CBM2 specific header once it exists */

#ifdef WINDOWS_COMPILE
void mididrv_ui_reset_device_list(int device)
{
}

char *mididrv_ui_get_next_device_name(int device, int *id)
{
    return NULL;
}
#endif

/*******************************************************************************
    clockport
*******************************************************************************/

clockport_supported_devices_t clockport_supported_devices[] = { { 0, NULL } };

/*******************************************************************************
    userport devices
*******************************************************************************/

bool pia1_get_diagnostic_pin(void)
{
    return false;
}

int parallel_cable_cpu_resources_init(void)
{
    return -1;
}
#ifndef __LIBRETRO__
int rsuser_cmdline_options_init(void)
{
    return -1;
}
int rsuser_resources_init(void)
{
    return -1;
}
#endif
