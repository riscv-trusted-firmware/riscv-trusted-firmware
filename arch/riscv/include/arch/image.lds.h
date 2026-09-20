/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Common linker script body. An image's <name>.ld.S defines IMAGE_BASE and
 * IMAGE_SIZE, then includes this file. The script goes through the C
 * preprocessor, so Kconfig symbols are available.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
	image (rwx) : ORIGIN = IMAGE_BASE, LENGTH = IMAGE_SIZE
}

SECTIONS {
	. = IMAGE_BASE;
	__image_start = .;

	.text : {
		KEEP(*(.text.entry))
		*(.text.trap)
		*(.text .text.*)
	} > image

	. = ALIGN(16);
	.rodata : {
		*(.rodata .rodata.*)
		*(.srodata .srodata.*)
		/* Constants that hold addresses, in a position-independent image. */
		*(.data.rel.ro .data.rel.ro.*)

		. = ALIGN(8);
		__service_table_start = .;
		KEEP(*(.service_table))
		__service_table_end = .;

		. = ALIGN(8);
		__driver_table_start = .;
		KEEP(*(.driver_table))
		__driver_table_end = .;
	} > image

#ifdef IMAGE_PIE
	/*
	 * A position-independent image carries its R_RISCV_RELATIVE
	 * relocations and applies them itself (entry.S). Part of the loaded
	 * image, read-only once that is done.
	 */
	.rela.dyn : ALIGN(8) {
		__rela_start = .;
		*(.rela .rela.*)
		__rela_end = .;
	} > image
#endif

	/* A protection boundary (Smepmp: R-X before, RW after). */
	.data : ALIGN(4096) {
		__text_rodata_end = .;
		__data_start = .;
		*(.data .data.*)
		*(.got .got.*)
		. = ALIGN(8);
		__global_pointer$ = . + 0x800;
		*(.sdata .sdata.*)
		. = ALIGN(8);
		__data_end = .;
	} > image

	.bss (NOLOAD) : {
		. = ALIGN(16);
		__bss_start = .;
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(16);
		__bss_end = .;
	} > image

#ifdef IMAGE_HEAP
	/*
	 * What is left of the image's memory: the stacks from the top down,
	 * one per hart that shows up in the hart table, and from the bottom
	 * up the heap (<heap.h>), for what is sized by the machine: the number
	 * of harts and domains, which the device tree tells at boot.
	 */
	.heap (NOLOAD) : {
		. = ALIGN(16);
		__heap_start = .;
	} > image
	__image_end = __heap_start;
	ASSERT(IMAGE_BASE + IMAGE_SIZE - __heap_start >= 4 * CONFIG_STACK_SIZE,
	       "no room for stacks and heap")
#else
	.stacks (NOLOAD) : {
		. = ALIGN(16);
		__stack_bottom = .;
		. += CONFIG_STACK_SIZE * CONFIG_PLATFORM_HART_COUNT;
		__stack_top = .;
	} > image

	__image_end = .;
#endif

#ifndef IMAGE_PIE
	/*
	 * GNU ld on RISC-V routes input .rela.* sections here even for a
	 * static link; the output must stay empty (checked below).
	 */
	.rela.dyn : { *(.rela .rela.*) }
#endif

	/* Non-allocated metadata, listed so --orphan-handling stays quiet. */
	.riscv.attributes 0 : { *(.riscv.attributes) }
	.comment 0 : { *(.comment) }
	.symtab 0 : { *(.symtab) }
	.strtab 0 : { *(.strtab) }
	.shstrtab 0 : { *(.shstrtab) }
	.debug_abbrev 0 : { *(.debug_abbrev) }
	.debug_addr 0 : { *(.debug_addr) }
	.debug_aranges 0 : { *(.debug_aranges) }
	.debug_frame 0 : { *(.debug_frame) }
	.debug_info 0 : { *(.debug_info) }
	.debug_line 0 : { *(.debug_line) }
	.debug_line_str 0 : { *(.debug_line_str) }
	.debug_loc 0 : { *(.debug_loc) }
	.debug_loclists 0 : { *(.debug_loclists) }
	.debug_macinfo 0 : { *(.debug_macinfo) }
	.debug_macro 0 : { *(.debug_macro) }
	.debug_names 0 : { *(.debug_names) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	.debug_pubtypes 0 : { *(.debug_pubtypes) }
	.debug_ranges 0 : { *(.debug_ranges) }
	.debug_rnglists 0 : { *(.debug_rnglists) }
	.debug_str 0 : { *(.debug_str) }
	.debug_str_offsets 0 : { *(.debug_str_offsets) }
	.debug_types 0 : { *(.debug_types) }

	/DISCARD/ : {
		*(.eh_frame .eh_frame_hdr)
		*(.note .note.*)
		*(.interp .dynamic .dynsym .dynstr .hash .gnu.hash)
		*(.gnu.version .gnu.version_d .gnu.version_r .plt .got.plt .dynbss)
	}
}

ASSERT(__image_end - __image_start <= IMAGE_SIZE, "image exceeds IMAGE_SIZE")
ASSERT(__bss_start % 16 == 0, ".bss is not 16-byte aligned")
#ifndef IMAGE_PIE
ASSERT(SIZEOF(.rela.dyn) == 0, "unexpected dynamic relocations")
#endif
