/*
 * mainc64cpu.c - Emulation of the C64 6510 processor.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andreas Boose <viceteam@t-online.de>
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

#include "6510core.h"
#include "alarm.h"
#include "archdep.h"
#include "autostart.h"

#ifdef FEATURE_CPUMEMHISTORY
#include "c64pla.h"
#endif

#include "debug.h"
#include "cmdline.h"
#include "interrupt.h"
#include "log.h"
#include "machine.h"
#include "mainc64cpu.h"
#include "maincpu.h"
#include "mainlock.h"
#include "mem.h"
#include "monitor.h"
#include "mos6510.h"
#include "reu.h"
#include "resources.h"
#include "snapshot.h"
#include "traps.h"
#include "types.h"

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

log_t maincpu_log = LOG_DEFAULT;

/* MACHINE_STUFF should define/undef

 - NEED_REG_PC

*/

/* ------------------------------------------------------------------------- */
#if defined (DEBUG) || defined (FEATURE_CPUMEMHISTORY)
CLOCK debug_clk;
#endif

CLOCK stolen_cycles;

static int reu_dma_triggered = 0;

#define NEED_REG_PC

/* ------------------------------------------------------------------------- */

/* Implement the hack to make opcode fetches faster.  */
#define JUMP(addr)                                                                         \
    do {                                                                                   \
        reg_pc = (unsigned int)(addr);                                                     \
        if (reg_pc >= (unsigned int)bank_limit || reg_pc < (unsigned int)bank_start) {     \
            mem_mmu_translate((unsigned int)(addr), &bank_base, &bank_start, &bank_limit); \
        }                                                                                  \
    } while (0)

/* ------------------------------------------------------------------------- */

int check_ba_low = 0;

inline static void interrupt_delay(void)
{
    while (maincpu_clk >= alarm_context_next_pending_clk(maincpu_alarm_context)) {
        alarm_context_dispatch(maincpu_alarm_context, maincpu_clk);
    }

    if (maincpu_int_status->irq_clk <= maincpu_clk) {
        maincpu_int_status->irq_delay_cycles++;
    }

    if (maincpu_int_status->nmi_clk <= maincpu_clk) {
        maincpu_int_status->nmi_delay_cycles++;
    }
}

static void maincpu_steal_cycles(void)
{
    interrupt_cpu_status_t *cs = maincpu_int_status;
    uint8_t opcode;

    if (maincpu_ba_low_flags & MAINCPU_BA_LOW_VICII) {
        vicii_steal_cycles();
        maincpu_ba_low_flags &= ~MAINCPU_BA_LOW_VICII;
    }

    if (maincpu_ba_low_flags & MAINCPU_BA_LOW_REU) {
        reu_dma_start();
        maincpu_ba_low_flags &= ~MAINCPU_BA_LOW_REU;
    }

    while (maincpu_clk >= alarm_context_next_pending_clk(maincpu_alarm_context)) {
        alarm_context_dispatch(maincpu_alarm_context, maincpu_clk);
    }

    /* special handling for steals during opcodes */
    opcode = OPINFO_NUMBER(*cs->last_opcode_info_ptr);
    switch (opcode) {
        /* SHA */
        case 0x93:
            if (check_ba_low) {
                OPINFO_SET_ENABLES_IRQ(*cs->last_opcode_info_ptr, 1);
            }
            break;

        /* SHS */
        case 0x9b:
            /* (fall through) */
        /* SHY */
        case 0x9c:
            /* (fall through) */
        /* SHX */
        case 0x9e:
            /* (fall through) */
        /* SHA */
        case 0x9f:
        /* this is a hacky way of signaling SET_ABS_SH_I() that
           cycles were stolen before the write */
            if (check_ba_low) {
                OPINFO_SET_ENABLES_IRQ(*cs->last_opcode_info_ptr, 1);
            }
            break;

        /* ANE */
        case 0x8b:
        /* this is a hacky way of signaling ANE() that
           cycles were stolen after the first fetch */
        /* (fall through) */

        /* LXA */
        case 0xab:
        /* this is a hacky way of signaling LXA() that
           cycles were stolen after the first fetch */
        /* (fall through) */

        /* CLI */
        case 0x58:
            /* this is a hacky way of signaling CLI() that it
               shouldn't delay the interrupt */
            OPINFO_SET_ENABLES_IRQ(*cs->last_opcode_info_ptr, 1);
            break;

        default:
            break;
    }

    /* SEI: do not update interrupt delay counters */
    if (opcode != 0x78) {
        if (cs->irq_delay_cycles == 0 && cs->irq_clk < maincpu_clk) {
            cs->irq_delay_cycles++;
        }
    }

    if (cs->nmi_delay_cycles == 0 && cs->nmi_clk < maincpu_clk) {
        cs->nmi_delay_cycles++;
    }
}

inline static void check_ba(void)
{
    if (maincpu_ba_low_flags) {
        CLOCK old_maincpu_clk = maincpu_clk;

        maincpu_steal_cycles();
#if defined (DEBUG) || defined (FEATURE_CPUMEMHISTORY)
        if (debug_clk == old_maincpu_clk) {
            debug_clk = maincpu_clk;
        }
#endif

        stolen_cycles += maincpu_clk - old_maincpu_clk;
    }
}

#ifdef FEATURE_CPUMEMHISTORY

/* FIXME do proper ROM/RAM/IO tests */

/* this is called per memory access, so it should only do whats really needed */
inline static void memmap_mem_update(unsigned int addr, int write, int dummy)
{
    unsigned int type = MEMMAP_RAM_R;

    if (write) {
        if ((addr >= 0xd000) && (addr <= 0xdfff)) {
            type = MEMMAP_I_O_W;
        } else {
            type = MEMMAP_RAM_W;
        }
    } else {
        switch (addr >> 12) {
            case 0xa:
            case 0xb:
            case 0xe:
            case 0xf:
                if (pport.data_read & (1 << ((addr >> 14) & 1))) {
                    type = MEMMAP_ROM_R;
                } else {
                    type = MEMMAP_RAM_R;
                }
                break;
            case 0xd:
                type = MEMMAP_I_O_R;
                break;
            default:
                type = MEMMAP_RAM_R;
                break;
        }
        if (memmap_state & MEMMAP_STATE_OPCODE) {
            /* HACK: transform R to X */
            type >>= 2;
            memmap_state &= ~(MEMMAP_STATE_OPCODE);
#if 0
        } else if (memmap_state & MEMMAP_STATE_INSTR) {
            /* ignore operand reads */
            type = 0;
#endif
        }
        if (dummy == 0) {
            type |= MEMMAP_REGULAR_READ;
        }
    }
    monitor_memmap_store(addr, type);
}

static void memmap_mem_store(unsigned int addr, unsigned int value)
{
    memmap_mem_update(addr, 1, 0);
    (*_mem_write_tab_ptr[(addr) >> 8])((uint16_t)(addr), (uint8_t)(value));
}

static void memmap_mem_store_dummy(unsigned int addr, unsigned int value)
{
    memmap_mem_update(addr, 1, 1);
    (*_mem_write_tab_ptr_dummy[(addr) >> 8])((uint16_t)(addr), (uint8_t)(value));
}

/* read byte, check BA and mark as read */
static uint8_t memmap_mem_read(unsigned int addr)
{
    check_ba();
    memmap_mem_update(addr, 0, 0);
    return (*_mem_read_tab_ptr[(addr) >> 8])((uint16_t)(addr));
}

static uint8_t memmap_mem_read_dummy(unsigned int addr)
{
    check_ba();
    memmap_mem_update(addr, 0, 1);
    return (*(_mem_read_tab_ptr_dummy[(addr) >> 8]))((uint16_t)(addr));
}

#ifndef STORE
#define STORE(addr, value) \
    if (reu_dma_triggered == 0) { \
        memmap_mem_store(addr, value); \
        if (addr == 0xff00) { \
            reu_dma(-1); \
        } \
    } \
    reu_dma_triggered = 0
#endif

#ifndef STORE_DUMMY
#define STORE_DUMMY(addr, value) \
    memmap_mem_store_dummy(addr, value); \
    if (addr == 0xff00) { \
        reu_dma_triggered = reu_dma(-1); \
    }
#endif

#ifndef LOAD
#define LOAD(addr) \
    memmap_mem_read(addr)
#endif

#ifndef LOAD_DUMMY
#define LOAD_DUMMY(addr) \
    memmap_mem_read_dummy(addr)
#endif

#ifndef LOAD_CHECK_BA_LOW
#define LOAD_CHECK_BA_LOW(addr) \
    check_ba_low = 1;           \
    memmap_mem_read(addr);\
    check_ba_low = 0
#endif

#ifndef LOAD_CHECK_BA_LOW_DUMMY
#define LOAD_CHECK_BA_LOW_DUMMY(addr) \
    check_ba_low = 1;           \
    memmap_mem_read_dummy(addr);\
    check_ba_low = 0
#endif

#ifndef STORE_ZERO
#define STORE_ZERO(addr, value) \
    memmap_mem_store((addr) & 0xff, value)
#endif

#ifndef STORE_ZERO_DUMMY
#define STORE_ZERO_DUMMY(addr, value) \
    memmap_mem_store_dummy((addr) & 0xff, value)
#endif

#ifndef LOAD_ZERO
#define LOAD_ZERO(addr) \
    memmap_mem_read((addr) & 0xff)
#endif

#ifndef LOAD_ZERO_DUMMY
#define LOAD_ZERO_DUMMY(addr) \
    memmap_mem_read_dummy((addr) & 0xff)
#endif

/* Route stack operations through memmap */

#define PUSH(val) memmap_mem_store((0x100 + (reg_sp--)), (uint8_t)(val))
#define PULL()    memmap_mem_read(0x100 + (++reg_sp))
#define STACK_PEEK()  memmap_mem_read_dummy(0x100 + reg_sp)

#endif /* FEATURE_CPUMEMHISTORY */

inline static uint8_t mem_read_check_ba(unsigned int addr)
{
    check_ba();
    return (*_mem_read_tab_ptr[(addr) >> 8])((uint16_t)(addr));
}

inline static uint8_t mem_read_check_ba_dummy(unsigned int addr)
{
    check_ba();
    return (*(_mem_read_tab_ptr_dummy[(addr) >> 8]))((uint16_t)(addr));
}

#ifndef STORE
#define STORE(addr, value) \
    if (reu_dma_triggered == 0) { \
        (*_mem_write_tab_ptr[(addr) >> 8])((uint16_t)(addr), (uint8_t)(value)); \
        if (addr == 0xff00) { \
            reu_dma(-1); \
        } \
    } \
    reu_dma_triggered = 0
#endif

#ifndef STORE_DUMMY
#define STORE_DUMMY(addr, value) \
    (*_mem_write_tab_ptr_dummy[(addr) >> 8])((uint16_t)(addr), (uint8_t)(value)); \
    if (addr == 0xff00) { \
        reu_dma_triggered = reu_dma(-1); \
    }
#endif

#ifndef LOAD
#define LOAD(addr) \
    mem_read_check_ba(addr)
#endif

#ifndef LOAD_DUMMY
#define LOAD_DUMMY(addr) \
    mem_read_check_ba_dummy(addr)
#endif

#ifndef LOAD_CHECK_BA_LOW
#define LOAD_CHECK_BA_LOW(addr) \
    check_ba_low = 1;           \
    mem_read_check_ba(addr);    \
    check_ba_low = 0
#endif

#ifndef LOAD_CHECK_BA_LOW_DUMMY
#define LOAD_CHECK_BA_LOW_DUMMY(addr) \
    check_ba_low = 1;                 \
    mem_read_check_ba_dummy(addr);    \
    check_ba_low = 0
#endif

#ifndef STORE_ZERO
#define STORE_ZERO(addr, value) \
    (*_mem_write_tab_ptr[0])((uint16_t)(addr), (uint8_t)(value))
#endif

#ifndef STORE_ZERO_DUMMY
#define STORE_ZERO_DUMMY(addr, value) \
    (*_mem_write_tab_ptr_dummy[0])((uint16_t)(addr), (uint8_t)(value))
#endif

#ifndef LOAD_ZERO
#define LOAD_ZERO(addr) \
    mem_read_check_ba((addr) & 0xff)
#endif

#ifndef LOAD_ZERO_DUMMY
#define LOAD_ZERO_DUMMY(addr) \
    mem_read_check_ba_dummy((addr) & 0xff)
#endif

/* Route stack operations through read/write handlers */

#ifndef PUSH
#define PUSH(val) (*_mem_write_tab_ptr[0x01])((uint16_t)(0x100 + (reg_sp--)), (uint8_t)(val))
#endif

#ifndef PULL
#define PULL()    mem_read_check_ba(0x100 + (++reg_sp))
#endif

#ifndef STACK_PEEK
#define STACK_PEEK()  mem_read_check_ba_dummy(0x100 + reg_sp)
#endif

#ifndef DMA_FUNC
static void maincpu_generic_dma(void)
{
    /* Generic DMA hosts can be implemented here.
       For example a very accurate REU emulation. */
}
#define DMA_FUNC maincpu_generic_dma()
#endif

#ifndef DMA_ON_RESET
#define DMA_ON_RESET
#endif

#ifndef CPU_ADDITIONAL_RESET
#define CPU_ADDITIONAL_RESET()
#endif

#ifndef CPU_ADDITIONAL_INIT
#define CPU_ADDITIONAL_INIT()
#endif

/* ------------------------------------------------------------------------- */

struct interrupt_cpu_status_s *maincpu_int_status = NULL;
alarm_context_t *maincpu_alarm_context = NULL;
monitor_interface_t *maincpu_monitor_interface = NULL;

/* This flag is an obsolete optimization. It's always 0 for the x64sc CPU,
   but has to be kept for the common code. */
int maincpu_rmw_flag = 0;

/* Information about the last executed opcode.  This is used to know the
   number of write cycles in the last executed opcode and to delay interrupts
   by one more cycle if necessary, as happens with conditional branch opcodes
   when the branch is taken.  */
unsigned int last_opcode_info;

/* Address of the last executed opcode. This is used by watchpoints. */
unsigned int last_opcode_addr;

/* Number of write cycles for each 6510 opcode.  */
const CLOCK maincpu_opcode_write_cycles[] = {
            /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    /* $00 */  3, 0, 0, 2, 0, 0, 2, 2, 1, 0, 0, 0, 0, 0, 2, 2, /* $00 */
    /* $10 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2, /* $10 */
    /* $20 */  2, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, /* $20 */
    /* $30 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2, /* $30 */
    /* $40 */  0, 0, 0, 2, 0, 0, 2, 2, 1, 0, 0, 0, 0, 0, 2, 2, /* $40 */
    /* $50 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2, /* $50 */
    /* $60 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, /* $60 */
    /* $70 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2, /* $70 */
    /* $80 */  0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, /* $80 */
    /* $90 */  0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, /* $90 */
    /* $A0 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* $A0 */
    /* $B0 */  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* $B0 */
    /* $C0 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, /* $C0 */
    /* $D0 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2, /* $D0 */
    /* $E0 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, /* $E0 */
    /* $F0 */  0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 0, 2, 0, 0, 2, 2  /* $F0 */
            /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
};

/* Public copy of the CPU registers.  As putting the registers into the
   function makes it faster, you have to generate a `TRAP' interrupt to have
   the values copied into this struct.  */
mos6510_regs_t maincpu_regs;

static int maincpu_jammed = 0;

/* ------------------------------------------------------------------------- */

static int ane_log_level = 0; /* 0: none, 1: unstable only 2: all */
static int lxa_log_level = 0; /* 0: none, 1: unstable only 2: all */

static int set_ane_log_level(int val, void *param)
{
    if ((val < 0) || (val > 2)) {
        return -1;
    }
    ane_log_level = val;
    return 0;
}

static int set_lxa_log_level(int val, void *param)
{
    if ((val < 0) || (val > 2)) {
        return -1;
    }
    lxa_log_level = val;
    return 0;
}

static const resource_int_t maincpu_resources_int[] = {
    { "LogLevelANE", 0, RES_EVENT_NO, NULL,
      &ane_log_level, set_ane_log_level, NULL },
    { "LogLevelLXA", 0, RES_EVENT_NO, NULL,
      &lxa_log_level, set_lxa_log_level, NULL },
    RESOURCE_INT_LIST_END
};

int maincpu_resources_init(void)
{
    return resources_register_int(maincpu_resources_int);
}

static const cmdline_option_t cmdline_options_maincpu[] =
{
    { "-aneloglevel", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "LogLevelANE", NULL,
      "<Type>", "Set ANE log level: (0: None, 1: Unstable, 2: All)" },
    { "-lxaloglevel", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "LogLevelLXA", NULL,
      "<Type>", "Set LXA log level: (0: None, 1: Unstable, 2: All)" },
    CMDLINE_LIST_END
};

int maincpu_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options_maincpu);
}

/* ------------------------------------------------------------------------- */

monitor_interface_t *maincpu_monitor_interface_get(void)
{
    maincpu_monitor_interface->cpu_regs = &maincpu_regs;
    maincpu_monitor_interface->cpu_R65C02_regs = NULL;
    maincpu_monitor_interface->dtv_cpu_regs = NULL;
    maincpu_monitor_interface->z80_cpu_regs = NULL;
    maincpu_monitor_interface->h6809_cpu_regs = NULL;

    maincpu_monitor_interface->int_status = maincpu_int_status;

    maincpu_monitor_interface->clk = &maincpu_clk;

    maincpu_monitor_interface->current_bank = 0;
    maincpu_monitor_interface->current_bank_index = 0;

    maincpu_monitor_interface->mem_bank_list = mem_bank_list;
    maincpu_monitor_interface->mem_bank_list_nos = mem_bank_list_nos;

    maincpu_monitor_interface->mem_bank_from_name = mem_bank_from_name;
    maincpu_monitor_interface->mem_bank_index_from_bank = mem_bank_index_from_bank;
    maincpu_monitor_interface->mem_bank_flags_from_bank = mem_bank_flags_from_bank;

    maincpu_monitor_interface->mem_bank_read = mem_bank_read;
    maincpu_monitor_interface->mem_bank_peek = mem_bank_peek;
    maincpu_monitor_interface->mem_peek_with_config = mem_peek_with_config;
    maincpu_monitor_interface->mem_bank_write = mem_bank_write;
    maincpu_monitor_interface->mem_bank_poke = mem_bank_poke;

    maincpu_monitor_interface->mem_ioreg_list_get = mem_ioreg_list_get;

    maincpu_monitor_interface->toggle_watchpoints_func = mem_toggle_watchpoints;

    maincpu_monitor_interface->set_bank_base = NULL;
    maincpu_monitor_interface->get_line_cycle = machine_get_line_cycle;

    return maincpu_monitor_interface;
}

/* ------------------------------------------------------------------------- */

void maincpu_early_init(void)
{
    maincpu_int_status = interrupt_cpu_status_new();

    maincpu_log = log_open("Main CPU");
}

void maincpu_init(void)
{
    interrupt_cpu_status_init(maincpu_int_status, &last_opcode_info);

    /* cpu specifix additional init routine */
    CPU_ADDITIONAL_INIT();
}

void maincpu_shutdown(void)
{
    interrupt_cpu_status_destroy(maincpu_int_status);
}

static void cpu_reset(void)
{
    int preserve_monitor;

    preserve_monitor = maincpu_int_status->global_pending_int & IK_MONITOR;

    interrupt_cpu_status_reset(maincpu_int_status);

    if (preserve_monitor) {
        interrupt_monitor_trap_on(maincpu_int_status);
    }

    maincpu_clk = 6; /* # of clock cycles needed for RESET.  */

    /* CPU specific extra reset routine, currently only used
       for 8502 fast mode refresh cycle. */
    CPU_ADDITIONAL_RESET();

    /* Do machine-specific initialization.  */
    machine_reset();
}

void maincpu_reset(void)
{
    cpu_reset();
}

/* ------------------------------------------------------------------------- */

/* Return nonzero if a pending NMI should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the NMI line.  */
inline static int interrupt_check_nmi_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    unsigned int delay_cycles = INTERRUPT_DELAY;

    /* BRK (0x00) delays the NMI by one opcode.  */
    /* TODO DO_INTERRUPT sets last opcode to 0: can NMI occur right after IRQ? */
    if (OPINFO_NUMBER(*cs->last_opcode_info_ptr) == 0x00) {
        return 0;
    }

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr)) {
        delay_cycles++;
    }

    if (cs->nmi_delay_cycles >= delay_cycles) {
        return 1;
    }

    return 0;
}

/* Return nonzero if a pending IRQ should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the IRQ line.  */
inline static int interrupt_check_irq_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    unsigned int delay_cycles = INTERRUPT_DELAY;

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr)) {
        delay_cycles++;
    }

    if (cs->irq_delay_cycles >= delay_cycles) {
        if (!OPINFO_ENABLES_IRQ(*cs->last_opcode_info_ptr)) {
            return 1;
        } else {
            cs->global_pending_int |= IK_IRQPEND;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------- */

#ifdef NEED_REG_PC
/* FIXME: this should really be uint16_t, but it breaks things (eg trap17.prg) */
unsigned int reg_pc;
#endif

static bool bank_base_ready = false;
static uint8_t *bank_base = NULL;
static int bank_start = 0;
static int bank_limit = 0;

void maincpu_resync_limits(void)
{
    if (bank_base_ready) {
        mem_mmu_translate(reg_pc, &bank_base, &bank_start, &bank_limit);
    }
}

#ifdef __LIBRETRO__
void maincpu_mainloop(void)
{
#define ORIGIN_MEMSPACE (e_comp_space)
    /* Notice that using a struct for these would make it a lot slower (at
       least, on gcc 2.7.2.x).  */
    static uint8_t reg_a = 0;
    static uint8_t reg_x = 0;
    static uint8_t reg_y = 0;
    static uint8_t reg_p = 0;
    static uint8_t reg_sp = 0;
    static uint8_t flag_n = 0;
    static uint8_t flag_z = 0;
#ifndef NEED_REG_PC
    /* FIXME: this should really be uint16_t, but it breaks things (eg trap17.prg) */
    static unsigned int reg_pc;
#endif

static unsigned retro_mainloop = 0;
if (!retro_mainloop)
{
    retro_mainloop = 1;

    /*
     * Enable maincpu_resync_limits functionality .. in the old code
     * this is where the local stack var had its address copied to
     * the global.
     */
    bank_base_ready = true;

    machine_trigger_reset(MACHINE_RESET_MODE_RESET_CPU);
}
    /*while (1)*/ {
#define CPU_LOG_ID maincpu_log
#define ANE_LOG_LEVEL ane_log_level
#define LXA_LOG_LEVEL lxa_log_level
#define CPU_IS_JAMMED maincpu_jammed
#define CLK maincpu_clk
#define RMW_FLAG maincpu_rmw_flag
#define LAST_OPCODE_INFO last_opcode_info
#define LAST_OPCODE_ADDR last_opcode_addr
#define TRACEFLG debug.maincpu_traceflg

#define CPU_INT_STATUS maincpu_int_status

#define ALARM_CONTEXT maincpu_alarm_context

#define CHECK_PENDING_ALARM() (clk >= next_alarm_clk(maincpu_int_status))

#define CHECK_PENDING_INTERRUPT() check_pending_interrupt(maincpu_int_status)

#define TRAP(addr) maincpu_int_status->trap_func(addr);

#define ROM_TRAP_HANDLER() traps_handler()

#define JAM()                                                         \
    do {                                                              \
        unsigned int tmp;                                             \
                                                                      \
        EXPORT_REGISTERS();                                           \
        tmp = machine_jam("   " CPU_STR ": JAM at $%04X   ", reg_pc); \
        switch (tmp) {                                                \
            case JAM_RESET_CPU:                                       \
                DO_INTERRUPT(IK_RESET);                               \
                break;                                                \
            case JAM_POWER_CYCLE:                                     \
                machine_powerup();                                    \
                DO_INTERRUPT(IK_RESET);                               \
                break;                                                \
            case JAM_MONITOR:                                         \
                monitor_startup(e_comp_space);                        \
                IMPORT_REGISTERS();                                   \
                break;                                                \
            default:                                                  \
                CLK_INC();                                            \
        }                                                             \
    } while (0)

#define CALLER e_comp_space

#define ROM_TRAP_ALLOWED() mem_rom_trap_allowed((uint16_t)reg_pc)

#define GLOBAL_REGS maincpu_regs

#include "6510dtvcore.c"

        maincpu_int_status->num_dma_per_opcode = 0;

        if (maincpu_clk_limit && (maincpu_clk > maincpu_clk_limit)) {
            log_error(maincpu_log, "cycle limit reached.");
            archdep_vice_exit(EXIT_FAILURE);
        }

        autostart_advance();
#if 0
        if (CLK > 246171754) {
            debug.maincpu_traceflg = 1;
        }
#endif
    }
}

#else /* __LIBRETRO__ */

void maincpu_mainloop(void)
{
#define ORIGIN_MEMSPACE (e_comp_space)
    /* Notice that using a struct for these would make it a lot slower (at
       least, on gcc 2.7.2.x).  */
    uint8_t reg_a = 0;
    uint8_t reg_x = 0;
    uint8_t reg_y = 0;
    uint8_t reg_p = 0;
    uint8_t reg_sp = 0;
    uint8_t flag_n = 0;
    uint8_t flag_z = 0;
#ifndef NEED_REG_PC
    /* FIXME: this should really be uint16_t, but it breaks things (eg trap17.prg) */
    unsigned int reg_pc;
#endif

    /*
     * Enable maincpu_resync_limits functionality .. in the old code
     * this is where the local stack var had its address copied to
     * the global.
     */
    bank_base_ready = true;

    machine_trigger_reset(MACHINE_RESET_MODE_RESET_CPU);

    while (1) {
#define CPU_LOG_ID maincpu_log
#define ANE_LOG_LEVEL ane_log_level
#define LXA_LOG_LEVEL lxa_log_level
#define CPU_IS_JAMMED maincpu_jammed
#define CLK maincpu_clk
#define RMW_FLAG maincpu_rmw_flag
#define LAST_OPCODE_INFO last_opcode_info
#define LAST_OPCODE_ADDR last_opcode_addr
#define TRACEFLG debug.maincpu_traceflg

#define CPU_INT_STATUS maincpu_int_status

#define ALARM_CONTEXT maincpu_alarm_context

#define CHECK_PENDING_ALARM() (clk >= next_alarm_clk(maincpu_int_status))

#define CHECK_PENDING_INTERRUPT() check_pending_interrupt(maincpu_int_status)

#define TRAP(addr) maincpu_int_status->trap_func(addr);

#define ROM_TRAP_HANDLER() traps_handler()

#define JAM()                                                         \
    do {                                                              \
        unsigned int tmp;                                             \
                                                                      \
        EXPORT_REGISTERS();                                           \
        tmp = machine_jam("   " CPU_STR ": JAM at $%04X   ", reg_pc); \
        switch (tmp) {                                                \
            case JAM_RESET_CPU:                                       \
                DO_INTERRUPT(IK_RESET);                               \
                break;                                                \
            case JAM_POWER_CYCLE:                                     \
                machine_powerup();                                    \
                DO_INTERRUPT(IK_RESET);                               \
                break;                                                \
            case JAM_MONITOR:                                         \
                monitor_startup(e_comp_space);                        \
                IMPORT_REGISTERS();                                   \
                break;                                                \
            default:                                                  \
                CLK_INC();                                            \
        }                                                             \
    } while (0)

#define CALLER e_comp_space

#define ROM_TRAP_ALLOWED() mem_rom_trap_allowed((uint16_t)reg_pc)

#define GLOBAL_REGS maincpu_regs

#include "6510dtvcore.c"

        maincpu_int_status->num_dma_per_opcode = 0;

        if (maincpu_clk_limit && (maincpu_clk > maincpu_clk_limit)) {
            log_error(maincpu_log, "cycle limit reached.");
            archdep_vice_exit(EXIT_FAILURE);
        }

        autostart_advance();
#if 0
        if (CLK > 246171754) {
            debug.maincpu_traceflg = 1;
        }
#endif
    }
}
#endif /* __LIBRETRO__ */

/* ------------------------------------------------------------------------- */

void maincpu_set_pc(int pc) {
    MOS6510_REGS_SET_PC(&maincpu_regs, pc);
}

void maincpu_set_a(int a) {
    MOS6510_REGS_SET_A(&maincpu_regs, a);
}

void maincpu_set_x(int x) {
    MOS6510_REGS_SET_X(&maincpu_regs, x);
}

void maincpu_set_y(int y) {
    MOS6510_REGS_SET_Y(&maincpu_regs, y);
}

void maincpu_set_sign(int n) {
    MOS6510_REGS_SET_SIGN(&maincpu_regs, n);
}

void maincpu_set_zero(int z) {
    MOS6510_REGS_SET_ZERO(&maincpu_regs, z);
}

void maincpu_set_carry(int c) {
    MOS6510_REGS_SET_CARRY(&maincpu_regs, c);
}

void maincpu_set_interrupt(int i) {
    MOS6510_REGS_SET_INTERRUPT(&maincpu_regs, i);
}

unsigned int maincpu_get_pc(void) {
    return MOS6510_REGS_GET_PC(&maincpu_regs);
}

unsigned int maincpu_get_a(void) {
    return MOS6510_REGS_GET_A(&maincpu_regs);
}

unsigned int maincpu_get_x(void) {
    return MOS6510_REGS_GET_X(&maincpu_regs);
}

unsigned int maincpu_get_y(void) {
    return MOS6510_REGS_GET_Y(&maincpu_regs);
}

unsigned int maincpu_get_sp(void) {
    return MOS6510_REGS_GET_SP(&maincpu_regs);
}

/* ------------------------------------------------------------------------- */

#ifdef __LIBRETRO__
/* Old version kept due to new log level stuff breaking old snapshots */

static char snap_module_name[] = "MAINCPU";
#define SNAP_MAJOR 1
#define SNAP_MINOR 2

int maincpu_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, snap_module_name, ((uint8_t)SNAP_MAJOR),
                               ((uint8_t)SNAP_MINOR));
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMW_CLOCK(m, maincpu_clk) < 0
        || SMW_B(m, MOS6510_REGS_GET_A(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_X(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_Y(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_SP(&maincpu_regs)) < 0
        || SMW_W(m, (uint16_t)MOS6510_REGS_GET_PC(&maincpu_regs)) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_STATUS(&maincpu_regs)) < 0
        || SMW_DW(m, (uint32_t)last_opcode_info) < 0
        || SMW_DW(m, (uint32_t)maincpu_ba_low_flags) < 0) {
        goto fail;
    }

    if (interrupt_write_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_write_new_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_write_sc_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}

int maincpu_snapshot_read_module(snapshot_t *s)
{
    uint8_t a, x, y, sp, status;
    uint16_t pc;
    uint8_t major, minor;
    snapshot_module_t *m;

    m = snapshot_module_open(s, snap_module_name, &major, &minor);
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMR_CLOCK(m, &maincpu_clk) < 0
        || SMR_B(m, &a) < 0
        || SMR_B(m, &x) < 0
        || SMR_B(m, &y) < 0
        || SMR_B(m, &sp) < 0
        || SMR_W(m, &pc) < 0
        || SMR_B(m, &status) < 0
        || SMR_DW_UINT(m, &last_opcode_info) < 0
        || SMR_DW_INT(m, &maincpu_ba_low_flags) < 0) {
        goto fail;
    }

    MOS6510_REGS_SET_A(&maincpu_regs, a);
    MOS6510_REGS_SET_X(&maincpu_regs, x);
    MOS6510_REGS_SET_Y(&maincpu_regs, y);
    MOS6510_REGS_SET_SP(&maincpu_regs, sp);
    MOS6510_REGS_SET_PC(&maincpu_regs, pc);
    MOS6510_REGS_SET_STATUS(&maincpu_regs, status);

    if (interrupt_read_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_read_new_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_read_sc_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}

#else

static char snap_module_name[] = "MAINCPU";
#define SNAP_MAJOR 1
#define SNAP_MINOR 4

int maincpu_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, snap_module_name, ((uint8_t)SNAP_MAJOR),
                               ((uint8_t)SNAP_MINOR));
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMW_CLOCK(m, maincpu_clk) < 0
        || SMW_B(m, MOS6510_REGS_GET_A(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_X(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_Y(&maincpu_regs)) < 0
        || SMW_B(m, MOS6510_REGS_GET_SP(&maincpu_regs)) < 0
        || SMW_W(m, (uint16_t)MOS6510_REGS_GET_PC(&maincpu_regs)) < 0
        || SMW_B(m, (uint8_t)MOS6510_REGS_GET_STATUS(&maincpu_regs)) < 0
        || SMW_DW(m, (uint32_t)last_opcode_info) < 0
        || SMW_DW(m, (uint32_t)ane_log_level) < 0
        || SMW_DW(m, (uint32_t)lxa_log_level) < 0
        || SMW_DW(m, (uint32_t)maincpu_jammed) < 0
        || SMW_DW(m, (uint32_t)maincpu_ba_low_flags) < 0) {
        goto fail;
    }

    if (interrupt_write_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_write_new_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_write_sc_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}

int maincpu_snapshot_read_module(snapshot_t *s)
{
    uint8_t a, x, y, sp, status;
    uint16_t pc;
    uint8_t major, minor;
    snapshot_module_t *m;

    m = snapshot_module_open(s, snap_module_name, &major, &minor);
    if (m == NULL) {
        return -1;
    }

    if (0
        || SMR_CLOCK(m, &maincpu_clk) < 0
        || SMR_B(m, &a) < 0
        || SMR_B(m, &x) < 0
        || SMR_B(m, &y) < 0
        || SMR_B(m, &sp) < 0
        || SMR_W(m, &pc) < 0
        || SMR_B(m, &status) < 0
        || SMR_DW_UINT(m, &last_opcode_info) < 0
        || SMR_DW_INT(m, &ane_log_level) < 0
        || SMR_DW_INT(m, &lxa_log_level) < 0
        || SMR_DW_INT(m, &maincpu_jammed) < 0
        || SMR_DW_INT(m, &maincpu_ba_low_flags) < 0) {
        goto fail;
    }

    MOS6510_REGS_SET_A(&maincpu_regs, a);
    MOS6510_REGS_SET_X(&maincpu_regs, x);
    MOS6510_REGS_SET_Y(&maincpu_regs, y);
    MOS6510_REGS_SET_SP(&maincpu_regs, sp);
    MOS6510_REGS_SET_PC(&maincpu_regs, pc);
    MOS6510_REGS_SET_STATUS(&maincpu_regs, status);

    if (interrupt_read_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_read_new_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    if (interrupt_read_sc_snapshot(maincpu_int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL) {
        snapshot_module_close(m);
    }
    return -1;
}

#endif
