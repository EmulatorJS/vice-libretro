/*
 * c64dtvui.c - Definition of the C64DTV-specific part of the UI.
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
#include <stdlib.h>
#include <string.h>

#include "c64ui.h"
#include "lib.h"
#include "machine.h"
#include "menudefs.h"
#include "resources.h"
#include "tui.h"
#include "tuifs.h"
#include "tuimenu.h"
#include "types.h"
#include "ui.h"
#include "uic64dtv.h"
#include "uisiddtv.h"

TUI_MENU_DEFINE_TOGGLE(VICIIVideoCache)
TUI_MENU_DEFINE_TOGGLE(VICIICheckSsColl)
TUI_MENU_DEFINE_TOGGLE(VICIICheckSbColl)
TUI_MENU_DEFINE_TOGGLE(PALEmulation)

static TUI_MENU_CALLBACK(toggle_MachineVideoStandard_callback)
{
    int value;

    resources_get_int("MachineVideoStandard", &value);

    if (been_activated) {
            if (value == MACHINE_SYNC_PAL)
                value = MACHINE_SYNC_NTSC;
            else if (value == MACHINE_SYNC_NTSC)
                value = MACHINE_SYNC_NTSCOLD;
        else
                value = MACHINE_SYNC_PAL;

        resources_set_int("MachineVideoStandard", value);
    }

    switch (value) {
      case MACHINE_SYNC_PAL:
        return "PAL-G";
      case MACHINE_SYNC_NTSC:
        return "NTSC-M";
      default:
        return "(Custom)";
    }
}

static tui_menu_item_def_t vicii_menu_items[] = {
    { "Video _Cache:",
      "Enable screen cache (disabled when using triple buffering)",
      toggle_VICIIVideoCache_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "_PAL Emulation:",
      "Enable PAL emulation",
      toggle_PALEmulation_callback, NULL, 3,
      TUI_MENU_BEH_RESUME, NULL, NULL },
    { "--" },
    { "Sprite-_Background Collisions:",
      "Emulate sprite-background collision register",
      toggle_VICIICheckSbColl_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Sprite-_Sprite Collisions:",
      "Emulate sprite-sprite collision register",
      toggle_VICIICheckSsColl_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "V_ideo Standard:",
      "Select machine clock ratio",
      toggle_MachineVideoStandard_callback, NULL, 11,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { NULL }
};

/* ------------------------------------------------------------------------- */

TUI_MENU_DEFINE_TOGGLE(ps2mouse)
TUI_MENU_DEFINE_TOGGLE(Mouse)
TUI_MENU_DEFINE_TOGGLE(EmuID)

static tui_menu_item_def_t ioextenstions_menu_items[] = {
    { "Grab mouse events",
      "Emulate a mouse",
      toggle_Mouse_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "PS/2 mouse emulation",
      "Emulate a PS/2 mouse",
      toggle_ps2mouse_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "_Emulator Identification:",
      "Allow programs to identify the emulator they are running on",
      toggle_EmuID_callback, NULL, 3,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { NULL }
};

/* ------------------------------------------------------------------------- */

static struct {
    const char *name;
    const char *brief_description;
    const char *menu_item;
    const char *long_description;
} palette_items[] = {
    { "default", "Default", "_Default",
      "Default VICE C64 palette" },
    { "c64s", "C64S", "C64_S",
      "Palette from the C64S emulator by Miha Peternel" },
    { "ccs64", "CCS64", "_CCS64",
      "Palette from the CCS64 emulator by Per Hakan Sundell" },
    { "frodo", "Frodo", "_Frodo",
      "Palette from the Frodo emulator by Christian Bauer" },
    { "godot", "GoDot", "_GoDot",
      "Palette as suggested by the authors of the GoDot C64 graphics package" },    { "pc64", "PC64", "_PC64",
      "Palette from the PC64 emulator by Wolfgang Lorenz" },
    { NULL }
};

static TUI_MENU_CALLBACK(palette_callback)
{
    if (been_activated) {
        if (resources_set_string("VICIIPaletteFile", (const char *)param) < 0)
           tui_error("Invalid palette file");
        ui_update_menus();
    }
    return NULL;
}

static TUI_MENU_CALLBACK(custom_palette_callback)
{
    if (been_activated) {
        char *name;

        name = tui_file_selector("Load custom palette",
                                 NULL, "*.vpl", NULL, 0, NULL, 0, NULL, NULL);

        if (name != NULL) {
            if (resources_set_string("VICIIPaletteFile", name) < 0)
                tui_error("Invalid palette file");
            ui_update_menus();
            lib_free(name);
        }
    }
    return NULL;
}

static TUI_MENU_CALLBACK(palette_menu_callback)
{
    const char *s;
    int i;

    resources_get_string("VICIIPaletteFile", &s);
    for (i = 0; palette_items[i].name != NULL; i++) {
        if (strcmp(s, palette_items[i].name) == 0)
           return palette_items[i].brief_description;
    }
    return "Custom";
}

TUI_MENU_DEFINE_TOGGLE(VICIIExternalPalette)

static void add_palette_submenu(tui_menu_t parent)
{
    int i;
    tui_menu_t palette_menu = tui_menu_create("Color Set", 1);

    for (i = 0; palette_items[i].name != NULL; i++)
        tui_menu_add_item(palette_menu,
                          palette_items[i].menu_item,
                          palette_items[i].long_description,
                          palette_callback,
                          (void *) palette_items[i].name, 0,
                          TUI_MENU_BEH_RESUME);

    tui_menu_add_item(palette_menu,
                      "C_ustom",
                      "Load a custom palette",
                      custom_palette_callback,
                      NULL, 0,
                      TUI_MENU_BEH_RESUME);

    tui_menu_add_item(parent, "Use external Palette",
                      "Use the palette file below",
                      toggle_VICIIExternalPalette_callback,
                      NULL, 3, TUI_MENU_BEH_RESUME);

    tui_menu_add_submenu(parent, "Color _Palette:",
                         "Choose color palette",
                         palette_menu,
                         palette_menu_callback,
                         NULL,
                         10);
}

/* ------------------------------------------------------------------------- */

static TUI_MENU_CALLBACK(load_rom_file_callback)
{
    if (been_activated) {
        char *name;

        name = tui_file_selector("Load ROM file",
                                 NULL, "*", NULL, 0, NULL, 0, NULL, NULL);

        if (name != NULL) {
            if (resources_set_string(param, name) < 0)
                ui_error("Could not load ROM file '%s'", name);
            lib_free(name);
        }
    }
    return NULL;
}

static tui_menu_item_def_t rom_menu_items[] = {
    { "--" },
    { "Load new _Kernal ROM...",
      "Load new Kernal ROM",
      load_rom_file_callback, "KernalName", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new _BASIC ROM...",
      "Load new BASIC ROM",
      load_rom_file_callback, "BasicName", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new _Character ROM...",
      "Load new Character ROM",
      load_rom_file_callback, "ChargenName", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new 15_41 ROM...",
      "Load new 1541 ROM",
      load_rom_file_callback, "DosName1541", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new 1541-_II ROM...",
      "Load new 1541-II ROM",
      load_rom_file_callback, "DosName1541ii", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new 15_71 ROM...",
      "Load new 1571 ROM",
      load_rom_file_callback, "DosName1571", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new 15_81 ROM...",
      "Load new 1581 ROM",
      load_rom_file_callback, "DosName1581", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new _2031 ROM...",
      "Load new 2031 ROM",
      load_rom_file_callback, "DosName2031", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { "Load new _1001 ROM...",
      "Load new 1001 ROM",
      load_rom_file_callback, "DosName1001", 0,
      TUI_MENU_BEH_CONTINUE, NULL, NULL },
    { NULL }
};

/* ------------------------------------------------------------------------- */

int c64dtvui_init(void)
{
    tui_menu_t ui_ioextensions_submenu;

    ui_create_main_menu(1, 1, 1, 2, 1);

    tui_menu_add_separator(ui_special_submenu);

    uic64dtv_init(ui_special_submenu);
    tui_menu_add_separator(ui_special_submenu);

    ui_ioextensions_submenu = tui_menu_create("I/O extensions", 1);
    tui_menu_add(ui_ioextensions_submenu, ioextenstions_menu_items);
    tui_menu_add_submenu(ui_special_submenu, "_I/O extensions...",
                         "Configure I/O extensions",
                         ui_ioextensions_submenu, NULL, 0,
                         TUI_MENU_BEH_CONTINUE);

    tui_menu_add_separator(ui_video_submenu);

    add_palette_submenu(ui_video_submenu);

    tui_menu_add(ui_video_submenu, vicii_menu_items);
    tui_menu_add(ui_sound_submenu, siddtv_ui_menu_items);
    tui_menu_add(ui_rom_submenu, rom_menu_items);

    return 0;
}

void c64dtvui_shutdown(void)
{
}
