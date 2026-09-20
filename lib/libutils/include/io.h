/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef IO_H
#define IO_H

/*
 * Memory that is not the program's alone: device registers, and memory
 * another agent (a hart, a microcontroller) writes to. The compiler may
 * neither drop, repeat nor reorder these accesses among themselves, and
 * each is one access of the width asked for.
 */

#include <compiler.h>
#include <stddef.h>
#include <stdint.h>
#include <types_ext.h>
#include <util.h>

/* A variable shared with another agent: read or written once, as it stands. */
#define READ_ONCE(p) __compiler_atomic_load(&(p))
#define WRITE_ONCE(p, v) __compiler_atomic_store(&(p), (v))

static inline void io_write8(vaddr_t addr, uint8_t val)
{
	*(volatile uint8_t *)addr = val;
}

static inline void io_write16(vaddr_t addr, uint16_t val)
{
	*(volatile uint16_t *)addr = val;
}

static inline void io_write32(vaddr_t addr, uint32_t val)
{
	*(volatile uint32_t *)addr = val;
}

static inline void io_write64(vaddr_t addr, uint64_t val)
{
	*(volatile uint64_t *)addr = val;
}

static inline uint8_t io_read8(vaddr_t addr)
{
	return *(volatile uint8_t *)addr;
}

static inline uint16_t io_read16(vaddr_t addr)
{
	return *(volatile uint16_t *)addr;
}

static inline uint32_t io_read32(vaddr_t addr)
{
	return *(volatile uint32_t *)addr;
}

static inline uint64_t io_read64(vaddr_t addr)
{
	return *(volatile uint64_t *)addr;
}

static inline void io_write32_off(vaddr_t base, size_t offset, uint32_t val)
{
	io_write32(base + offset, val);
}

static inline uint32_t io_read32_off(vaddr_t base, size_t offset)
{
	return io_read32(base + offset);
}

/* The field @mask selects, of the register at @base + @offset. */
static inline uint32_t io_read32_off_field(vaddr_t base, size_t offset,
					   uint32_t mask)
{
	return get_field_u32(io_read32_off(base, offset), mask);
}

static inline void io_write32_off_field(vaddr_t base, size_t offset,
					uint32_t mask, uint32_t val)
{
	io_write32_off(base, offset,
		       set_field_u32(io_read32_off(base, offset), mask, val));
}

static inline void io_mask8(vaddr_t addr, uint8_t val, uint8_t mask)
{
	io_write8(addr, (io_read8(addr) & ~mask) | (val & mask));
}

static inline void io_mask16(vaddr_t addr, uint16_t val, uint16_t mask)
{
	io_write16(addr, (io_read16(addr) & ~mask) | (val & mask));
}

static inline void io_mask32(vaddr_t addr, uint32_t val, uint32_t mask)
{
	io_write32(addr, (io_read32(addr) & ~mask) | (val & mask));
}

/*
 * Set and clear bits. io_clrsetbits32() clears first: a bit in both masks
 * ends up set.
 */
static inline void io_setbits32(vaddr_t addr, uint32_t set_mask)
{
	io_write32(addr, io_read32(addr) | set_mask);
}

static inline void io_clrbits32(vaddr_t addr, uint32_t clear_mask)
{
	io_write32(addr, io_read32(addr) & ~clear_mask);
}

static inline void io_clrsetbits32(vaddr_t addr, uint32_t clear_mask,
				   uint32_t set_mask)
{
	io_write32(addr, (io_read32(addr) & ~clear_mask) | set_mask);
}

static inline void io_setbits16(vaddr_t addr, uint16_t set_mask)
{
	io_write16(addr, io_read16(addr) | set_mask);
}

static inline void io_clrbits16(vaddr_t addr, uint16_t clear_mask)
{
	io_write16(addr, io_read16(addr) & ~clear_mask);
}

static inline void io_setbits8(vaddr_t addr, uint8_t set_mask)
{
	io_write8(addr, io_read8(addr) | set_mask);
}

static inline void io_clrbits8(vaddr_t addr, uint8_t clear_mask)
{
	io_write8(addr, io_read8(addr) & ~clear_mask);
}

#endif
