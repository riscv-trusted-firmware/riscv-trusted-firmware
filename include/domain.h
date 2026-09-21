/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#ifndef DOMAIN_H
#define DOMAIN_H

/*
 * Domains partition the machine among several pieces of S-mode software:
 * a domain has harts, memory regions with S/U-mode permissions, a next
 * stage of its own, and a say on whether it may reset or suspend the
 * system. A hart runs one domain at a time; what it can reach, which harts
 * it can name in SBI calls and which addresses it can hand to the monitor
 * all go by that domain.
 *
 * The model and its device tree binding are the ones proposed for
 * standardisation: "riscv,domain,config",
 * "riscv,domain,memregion" and "riscv,domain,instance" nodes, and a
 * "riscv,domain" phandle in what belongs to a domain, which for a cpu node
 * is the domain the hart is in at boot. Without any of it in the tree there
 * is the root domain alone: every hart, all memory that is not the monitor's.
 *
 * A hart can also move from one domain to another and back, synchronously:
 * domain_enter() and domain_exit(), with a context per domain and hart
 * that holds what S-mode software owns of the hart in between. That is how
 * a domain without harts of its own (a trusted environment next to an
 * operating system) gets to run.
 */

#include <arch/trap.h>
#include <hartmask.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types_ext.h>
#include <util.h>

/* Region permissions, as in the "regions" property. */
#define DOMAIN_PERM_M_R BIT(0)
#define DOMAIN_PERM_M_W BIT(1)
#define DOMAIN_PERM_M_X BIT(2)
#define DOMAIN_PERM_SU_R BIT(3)
#define DOMAIN_PERM_SU_W BIT(4)
#define DOMAIN_PERM_SU_X BIT(5)
#define DOMAIN_PERM_ENFORCE BIT(6)
#define DOMAIN_PERM_SU_SHIFT 3
#define DOMAIN_PERM_SU_RWX \
	(DOMAIN_PERM_SU_R | DOMAIN_PERM_SU_W | DOMAIN_PERM_SU_X)

/*
 * S-mode software of different domains shares nothing through the monitor:
 * what a service keeps for S-mode per hart, it keeps per domain and hart,
 * domain_keys() of them, the running one being this_domain_key(). What such
 * state has in hardware moves at the domain switch, see <arch/hart.h>.
 *
 * How many domains and harts there are is the device tree's to say, so
 * that state comes from the heap: domain_hart_alloc() once the domains are
 * known, domain_hart_slot() to get at one.
 */
#include <stddef.h>

unsigned int domain_keys(void);
void *domain_hart_alloc(size_t size);
void *domain_hart_slot(void *base, size_t size, unsigned int key,
		       unsigned int hart);
/* The running domain's, on the calling hart. */
void *this_domain_hart_slot(void *base, size_t size);

#ifdef CONFIG_DOMAINS

struct domain_region {
	paddr_t base;
	unsigned int order; /* size = 2^order; __RISCV_XLEN__: everything */
	unsigned int perm;
	bool mmio;
};

struct domain {
	char name[32];
	unsigned int index; /* 0 is the root domain */
	uint32_t phandle;
	/* harts that may run it, by index */
	struct hartmask possible;
	/* Atomic, written under the domain lock: */
	struct hartmask assigned; /* harts that run it now */
	/* harts away in another domain, to come back */
	struct hartmask parked;
	/* atomic: domain_stop() is at work */
	unsigned long stopping;
	struct domain_region *regions; /* sorted, smallest first */
	unsigned int nr_regions, max_regions;
	int boot_hart; /* hart index, -1: none */
	unsigned long next_addr;
	unsigned long next_arg1;
	unsigned long next_mode; /* PRV_S or PRV_U */
	bool next_arg1_given;
	bool reset_allowed;
	bool suspend_allowed;
	/*
	 * Bit per domain, by index: the ones that may have a hart enter this
	 * one.
	 */
	bitstr_t *entry_from;
};

/*
 * Boot hart, once the hart table and the tree are there: the root domain,
 * and the domains of the device tree if it defines any. 'next_addr' and
 * 'next_mode' are the next stage of the root domain, and of the boot hart's
 * if it names none.
 */
void domains_init(const void *fdt, unsigned long next_addr,
		  unsigned long next_mode);
/*
 * Boot hart, at the end of the boot: start every domain's boot hart at the
 * domain's next stage; 'fdt' is what a domain without a "next-arg1" gets.
 * The calling hart enters its own domain, or waits to be started if it is
 * nobody's boot hart.
 */
void __noreturn domains_start(unsigned long fdt);
/*
 * The tree of the boot hart's domain: its harts, its devices, no partitioning.
 */
int domains_fdt_fixup(void *fdt);

unsigned int domain_count(void);
struct domain *domain_by_index(unsigned int index);
/*
 * The domain of an instance node, as other nodes refer to it; NULL: none such.
 */
struct domain *domain_by_phandle(uint32_t phandle);
/* The domain the calling hart runs now. */
struct domain *this_domain(void);
/* Its index; 0 before there are domains. */
unsigned int this_domain_key(void);

/*
 * May S/U-mode of 'dom' access [addr, addr + size) the way 'perm' (SU bits)
 * says?
 */
bool domain_range_ok(const struct domain *dom, paddr_t addr, paddr_size_t size,
		     unsigned int perm);

static inline bool domain_hart_assigned(const struct domain *dom,
					unsigned int index)
{
	return hartmask_test(&dom->assigned, index);
}

/* Runs the domain, or is to come back to it. */
static inline bool domain_hart_member(const struct domain *dom,
				      unsigned int index)
{
	return hartmask_test(&dom->assigned, index) ||
	       hartmask_test(&dom->parked, index);
}

static inline bool domain_has_parked(const struct domain *dom)
{
	return !hartmask_empty_atomic(&dom->parked);
}

static inline bool domain_reset_allowed(const struct domain *dom)
{
	return dom->reset_allowed;
}

static inline bool domain_suspend_allowed(const struct domain *dom)
{
	return dom->suspend_allowed;
}

/*
 * Contexts (domain_context.c). The calls that take 'regs' are made from an
 * ecall of the hart that moves; on SBI_SUCCESS 'regs' is the register file
 * of the context the hart is in now, results included: write nothing back.
 * They may not return at all, where the hart ends up stopped.
 */

/*
 * Moving a hart is a matter of policy, and the policy is the tree's:
 * "riscv,entry-allowed-from" in an instance node lists the domains whose
 * software may have a hart enter it, and nothing may where it is absent.
 * The root domain has no node, all of memory, and no way in: it runs what
 * the machine boots into it and the harts it starts itself. Leaving a
 * domain nobody entered (the boot's hand-over from one domain to the
 * next) is entering the next one, and goes by the same list.
 */
bool domain_may_enter(const struct domain *from, const struct domain *target);

/*
 * Run 'target' on the calling hart until it exits: a new context at the
 * domain's next stage when the hart is its boot hart, stopped for the
 * domain to start it when it is not, or the context that exited before,
 * which sees its exit call return 'arg'.
 */
long domain_enter(struct trap_regs *regs, struct domain *target,
		  unsigned long arg);
/*
 * Back to the context that entered this one, which sees its enter call
 * return 'value'. Without one: on to a domain that has not run on this
 * hart yet and lets this one enter it.
 */
long domain_exit(struct trap_regs *regs, unsigned long value);
/*
 * The same two moves for a service that uses them as its transport (the
 * MPXY bridge, <mpxy.h>) rather than for a call that asks for them: the
 * context that is left has its registers as it is to find them, and gets
 * no (error, value) when the hart comes back. domain_switch_to() only
 * goes where the hart would run (domain_enterable()): a context that
 * waits to be entered again, or the boot of 'target' on its boot hart.
 * domain_switch_back() goes back to the context that entered this one;
 * without one, 'first' is booted if this hart is its boot hart and has
 * not done so yet, and that is all (SBI_ERR_DENIED).
 */
bool domain_enterable(const struct domain *target);
long domain_switch_to(struct trap_regs *regs, struct domain *target);
long domain_switch_back(struct trap_regs *regs, struct domain *first);
/* The domain whose context waits for this one to exit, -1 if none does. */
int domain_caller_key(void);
/* Start a stopped domain at its next stage, on its boot hart. */
long domain_start(struct domain *dom);
/*
 * Stop every hart that runs 'dom', whatever it does, and forget its
 * contexts; harts that came from another domain go back there, their
 * enter call failed. Returns once that is done, or not at all when the
 * calling hart is one of them.
 */
long domain_stop(struct trap_regs *regs, struct domain *dom);
/* Does any hart run it, or is to come back to it? */
bool domain_running(const struct domain *dom);

/* Boot hart, once the domains are known. */
void domain_contexts_init(void);
/* The HSM state machine starts S-mode on the calling hart. */
void domain_context_started(void);
/* The calling hart's domain is being stopped... */
bool domain_stop_pending(void);
/* The same about the domain of another hart. */
bool domain_stop_pending_on(unsigned int index);
/*
 * ...and this is the hart's part in it. 'regs' is its trap frame, NULL when
 * it has none (S-mode state is gone already).
 */
void domain_stop_self(struct trap_regs *regs);
/*
 * An S-mode IPI for a hart of the calling domain that is away: it waits there.
 */
void domain_ipi_parked(unsigned int index);

#else /* !CONFIG_DOMAINS */

struct domain;

static inline unsigned int domain_count(void)
{
	return 1;
}

static inline struct domain *this_domain(void)
{
	return NULL;
}

static inline unsigned int this_domain_key(void)
{
	return 0;
}

static inline bool domain_hart_assigned(const struct domain *dom,
					unsigned int index)
{
	return true;
}

static inline bool domain_hart_member(const struct domain *dom,
				      unsigned int index)
{
	return true;
}

static inline bool domain_has_parked(const struct domain *dom)
{
	return false;
}

static inline bool domain_reset_allowed(const struct domain *dom)
{
	return true;
}

static inline bool domain_suspend_allowed(const struct domain *dom)
{
	return true;
}

static inline void domain_context_started(void)
{
}

static inline bool domain_stop_pending(void)
{
	return false;
}

static inline bool domain_stop_pending_on(unsigned int index)
{
	return false;
}

static inline void domain_stop_self(struct trap_regs *regs)
{
}

static inline void domain_ipi_parked(unsigned int index)
{
}

#endif

#endif
