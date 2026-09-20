/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef ARCH_DOMAIN_STATE_H
#define ARCH_DOMAIN_STATE_H

/*
 * What domain_state.S moves between a hart's registers and a domain
 * context (domain_context.c): the floating-point registers with fcsr,
 * the vector registers with their CSRs. Not for anybody else.
 */

#include <stdbool.h>
#include <stdint.h>

struct vec_state;

void _fp_state_save(uint64_t *buf, bool is_double);
void _fp_state_restore(const uint64_t *buf, bool is_double);
void _vec_state_save(struct vec_state *state);
void _vec_state_restore(const struct vec_state *state);
void _vec_state_clear(void);

#endif
