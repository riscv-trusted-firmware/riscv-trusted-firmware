/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef BITSTRING_H
#define BITSTRING_H

/*
 * Strings of bits, with the interface the BSDs have had since 4.4 in
 * <bitstring.h>: a bit string is an array of bitstr_t, declared with
 * bit_decl() and handled by bit number.
 *
 * One thing differs on purpose: a bitstr_t is an unsigned long, not a
 * byte. A word is what the atomics work on (<atomic.h>), and a bit string
 * that several harts set and clear bits of, a hart mask say, is changed a
 * word at a time with _bit_word() and _bit_mask(). A string is never read
 * or written in any other unit, so nothing depends on the order of bits in
 * memory.
 *
 *   bitstr_size(nbits)               bitstr_t elements for nbits bits
 *   bit_decl(name, nbits)            declares 'name', a string of nbits
 *   bit_test(name, bit)              non-zero when the bit is set
 *   bit_set(name, bit), bit_clear(name, bit)
 *   bit_nset(name, start, stop), bit_nclear(name, start, stop)
 *                                    bits start to stop, both included
 *   bit_ffs(name, nbits, value)      *value = the first bit set, -1: none
 *   bit_ffc(name, nbits, value)      ... the first bit clear
 *   bit_ffs_from(name, nbits, startbit, value)
 *                                    ... the first bit set from startbit on
 *   bit_foreach(bit, name, nbits)    a loop over the bits that are set
 */

#ifndef __ASSEMBLER__

typedef unsigned long bitstr_t;

#define _BITSTR_BITS (8 * (int)sizeof(bitstr_t))

/* The word a bit is in, and the bit as a mask of that word. */
#define _bit_word(bit) ((bit) / _BITSTR_BITS)
#define _bit_mask(bit) ((bitstr_t)1 << ((bit) % _BITSTR_BITS))

#define bitstr_size(nbits) (((nbits) + _BITSTR_BITS - 1) / _BITSTR_BITS)

#define bit_decl(name, nbits) bitstr_t name[bitstr_size(nbits)]

#define bit_test(name, bit) ((name)[_bit_word(bit)] & _bit_mask(bit))
#define bit_set(name, bit) ((name)[_bit_word(bit)] |= _bit_mask(bit))
#define bit_clear(name, bit) ((name)[_bit_word(bit)] &= ~_bit_mask(bit))

#define bit_nclear(name, start, stop)                                   \
	do {                                                            \
		bitstr_t *__bit_name = (name);                          \
		int __bit_stop = (stop);                                \
									\
		for (int __bit = (start); __bit <= __bit_stop; __bit++) \
			bit_clear(__bit_name, __bit);                   \
	} while (0)

#define bit_nset(name, start, stop)                                     \
	do {                                                            \
		bitstr_t *__bit_name = (name);                          \
		int __bit_stop = (stop);                                \
									\
		for (int __bit = (start); __bit <= __bit_stop; __bit++) \
			bit_set(__bit_name, __bit);                     \
	} while (0)

#define bit_ffs_from(name, nbits, startbit, value)                           \
	do {                                                                 \
		const bitstr_t *__bit_name = (name);                         \
		int __bit_nbits = (nbits), __bit_found = -1;                 \
									     \
		for (int __bit = (startbit); __bit < __bit_nbits; __bit++) { \
			/* A word without a bit in it is skipped whole. */   \
			if (!(__bit % _BITSTR_BITS) &&                       \
			    !__bit_name[_bit_word(__bit)]) {                 \
				__bit += _BITSTR_BITS - 1;                   \
				continue;                                    \
			}                                                    \
			if (bit_test(__bit_name, __bit)) {                   \
				__bit_found = __bit;                         \
				break;                                       \
			}                                                    \
		}                                                            \
		*(value) = __bit_found;                                      \
	} while (0)

#define bit_ffs(name, nbits, value) bit_ffs_from((name), (nbits), 0, (value))

#define bit_ffc(name, nbits, value)                               \
	do {                                                      \
		const bitstr_t *__bit_name = (name);              \
		int __bit_nbits = (nbits), __bit_found = -1;      \
								  \
		for (int __bit = 0; __bit < __bit_nbits; __bit++) \
			if (!bit_test(__bit_name, __bit)) {       \
				__bit_found = __bit;              \
				break;                            \
			}                                         \
		*(value) = __bit_found;                           \
	} while (0)

#define bit_foreach(bit, name, nbits)             \
	for ((bit) = 0; (bit) < (nbits); (bit)++) \
		if (bit_test((name), (bit)))

#endif /* !__ASSEMBLER__ */

#endif
