/*
 * list for generating function prototypes, assembly entry generating,
 * and fill dispatch table.
 *
 * Each VDDFUNC(id, name) wires name_entry into DispatchTable[id].
 * Function IDs are defined in vxd_vdd.h; gaps in the numbering are fine
 * (*vdd keeps its default handler for unregistered slots).
 */

VDDFUNC(REGISTER_DISPLAY_DRIVER, register_display_driver)
VDDFUNC(GET_VDD_BANK, get_vdd_bank)
VDDFUNC(SET_VDD_BANK, set_vdd_bank)
VDDFUNC(RESET_BANK, reset_bank)
VDDFUNC(PRE_HIRES_TO_VGA, pre_hires_to_vga)
VDDFUNC(POST_HIRES_TO_VGA, post_hires_to_vga)
VDDFUNC(PRE_VGA_TO_HIRES, pre_vga_to_hires)
VDDFUNC(POST_VGA_TO_HIRES, post_vga_to_hires)
VDDFUNC(SAVE_REGISTERS, save_registers)
VDDFUNC(RESTORE_REGISTERS, restore_registers)
VDDFUNC(ENABLE_TRAPS, enable_traps)
VDDFUNC(DISABLE_TRAPS, disable_traps)
VDDFUNC(VIRTUALIZE_CRTC_IN, virtualize_crtc_in)
VDDFUNC(VIRTUALIZE_CRTC_OUT, virtualize_crtc_out)
VDDFUNC(DISPLAY_DRIVER_DISABLING, display_driver_disabling)
VDDFUNC(GET_TOTAL_VRAM_SIZE, get_total_vram_size)
VDDFUNC(GET_BANK_SIZE, get_bank_size)
VDDFUNCRET(SET_HIRES_MODE, set_hires_mode)
VDDFUNCRET(VESA_SUPPORT, vesa_support)
VDDFUNC(GET_CHIP_ID, get_chip_id)
VDDFUNCRET(CHECK_SCREEN_SWITCH_OK, check_screen_switch_ok)
VDDFUNC(PRE_INT_10_MODE_SET, pre_int_10_mode_set)
