/*
 * c64ui.c - Implementation of the C64-specific part of the UI.
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

#include <stdio.h>
#include <string.h>

#include "uiapi.h"
#include "cartridge.h"
#include "keyboard.h"
#include "resources.h"
#include "videoarch.h"
#include "machine.h"

#include <stdarg.h>

#include "libretro-core.h"
extern retro_log_printf_t log_cb;

int display_setting(int hwscale, int old_width)
{
   return 0;
}

ui_jam_action_t ui_jam_dialog(const char *format, ...)
{
   char message[512];

   va_list ap;
   va_start(ap, format);
   vsnprintf(message, sizeof(message), format, ap);
   va_end(ap);

   log_cb(RETRO_LOG_ERROR, "%s\n", message);
   machine_trigger_reset(MACHINE_RESET_MODE_HARD);

   return UI_JAM_NONE;
}

int c64ui_init(void)
{
   return 0;
}

void c64ui_shutdown(void)
{
}

int c128ui_init(void)
{
   return 0;
}
void c128ui_shutdown(void)
{
}

int c64dtvui_init(void)
{
   return 0;
}
void c64dtvui_shutdown(void)
{
}

int c64scui_init(void)
{
   return 0;
}
void c64scui_shutdown(void)
{
}

int scpu64ui_init(void)
{
   return 0;
}
void scpu64ui_shutdown(void)
{
}

int plus4ui_init(void)
{
   return 0;
}
void plus4ui_shutdown(void)
{
}

int vic20ui_init(void)
{
   return 0;
}
void vic20ui_shutdown(void)
{
}

int cbm5x0ui_init(void)
{
   return 0;
}
void cbm5x0ui_shutdown(void)
{
}

int cbm2ui_init(void)
{
   return 0;
}
void cbm2ui_shutdown(void)
{
}

int petui_init(void)
{
   return 0;
}
void petui_shutdown(void)
{
}
