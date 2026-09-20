/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef LINKER_TABLE_H
#define LINKER_TABLE_H

/*
 * Tables the linker gathers from the objects: entries in a section of
 * their own, between the __<name>_table_start and __<name>_table_end
 * symbols the linker script defines, see <arch/image.lds.h>.
 */

#define LINKER_TABLE_FOREACH(elem, name)                                     \
	for ((elem) = __##name##_table_start; (elem) < __##name##_table_end; \
	     (elem)++)

#endif
