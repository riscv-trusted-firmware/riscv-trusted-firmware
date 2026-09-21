// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * Domains: the model, its device tree binding, the access check and the
 * boot. See <domain.h>; what moves a hart between domains is in
 * domain_context.c.
 */

#include <arch/hart.h>
#include <arch/hsm.h>
#include <arch/pmp.h>
#include <domain.h>
#include <fdt_util.h>
#include <heap.h>
#include <libfdt.h>
#include <log.h>
#include <string.h>
#include <types_ext.h>
#include <util.h>

/* The root domain and the instances of the device tree, from the heap. */
static struct domain *domains;
static unsigned int nr_domains;
/* What a domain without a "next-arg1" of its own is given. */
static unsigned long root_arg1;

unsigned int domain_count(void)
{
	return nr_domains;
}

struct domain *domain_by_index(unsigned int index)
{
	return index < nr_domains ? &domains[index] : NULL;
}

bool domain_may_enter(const struct domain *from, const struct domain *target)
{
	return target->index && target->entry_from &&
	       bit_test(target->entry_from, from->index);
}

struct domain *domain_by_phandle(uint32_t phandle)
{
	for (unsigned int d = 1; phandle && d < nr_domains; d++)
		if (domains[d].phandle == phandle)
			return &domains[d];
	return NULL;
}

struct domain *this_domain(void)
{
	return this_hart()->domain;
}

unsigned int this_domain_key(void)
{
	const struct domain *dom = this_hart()->domain;

	return dom ? dom->index : 0;
}

/* Inclusive, so that the top of the address space fits. */
static unsigned long region_last(const struct domain_region *r)
{
	return r->order >= __RISCV_XLEN__ ? ~UL(0) :
					    r->base + BIT(r->order) - 1;
}

bool domain_range_ok(const struct domain *dom, paddr_t addr, paddr_size_t size,
		     unsigned int perm)
{
	unsigned long last = addr + size - 1;

	if (!size)
		return true;
	if (last < addr || !monitor_range_clear(addr, size))
		return false;
	/* Early in the boot: no domains yet. */
	if (!dom)
		return true;
	/*
	 * What the PMP does with an access: the first region that has
	 * anything of the range decides, and it has to have all of it.
	 */
	for (unsigned int i = 0; i < dom->nr_regions; i++) {
		const struct domain_region *r = &dom->regions[i];

		if (addr > region_last(r) || last < r->base)
			continue;
		return addr >= r->base && last <= region_last(r) &&
		       (r->perm & perm) == perm;
	}
	return false;
}

static int add_region(struct domain *dom, unsigned long base,
		      unsigned int order, unsigned int perm, bool mmio)
{
	unsigned int i = 0;

	if (dom->nr_regions == dom->max_regions)
		return -1;
	/* By size: a region inside another one comes first, as it has to. */
	for (i = dom->nr_regions++; i && dom->regions[i - 1].order > order; i--)
		dom->regions[i] = dom->regions[i - 1];
	dom->regions[i] = (struct domain_region){ base, order, perm, mmio };
	return 0;
}

static void regions_alloc(struct domain *dom, unsigned int max)
{
	dom->regions = heap_alloc_array(max, sizeof(*dom->regions));
	dom->max_regions = max;
}

static void root_domain_init(unsigned long next_addr, unsigned long next_mode)
{
	struct domain *root = &domains[0];

	*root = (struct domain){
		.name = "root",
		.boot_hart = (int)this_hart_index(),
		.next_addr = next_addr,
		.next_mode = next_mode,
		.reset_allowed = true,
		.suspend_allowed = true,
	};
	/*
	 * Everything; what is the monitor's is taken out before it gets here.
	 */
	regions_alloc(root, 1);
	add_region(root, 0, __RISCV_XLEN__, DOMAIN_PERM_SU_RWX, false);
	for (unsigned int i = 0; hart_by_index(i); i++) {
		hartmask_set(&root->possible, i);
		hartmask_set(&root->assigned, i);
		hart_by_index(i)->domain = root;
	}
	nr_domains = 1;
}

/*
 * The hart index behind a cpu node phandle, -1 when the monitor has no such
 * hart.
 */
static int cpu_hart_index(const void *fdt, uint32_t phandle)
{
	int cpu = fdt_node_offset_by_phandle(fdt, phandle);
	uint64_t hartid = 0;

	if (cpu < 0 || fdt_reg(fdt, cpu, 0, &hartid, NULL))
		return -1;
	return hart_index((unsigned long)hartid);
}

static uint64_t prop_u64(const void *fdt, int node, const char *name,
			 bool *given)
{
	int len = 0;
	const fdt32_t *val = fdt_getprop(fdt, node, name, &len);

	*given = val && len >= 8;
	return *given ? reg_pair_to_64(fdt32_to_cpu(val[0]),
				       fdt32_to_cpu(val[1])) :
			0;
}

static bool instance_node(const void *fdt, int node)
{
	return !fdt_node_check_compatible(fdt, node, "riscv,domain,instance");
}

static int instance_parse(const void *fdt, int node, struct domain *dom)
{
	const fdt32_t *list = NULL;
	const char *inherit = NULL, *name = NULL;
	bool given = false;
	int len = 0;

	*dom = (struct domain){
		.boot_hart = domains[0].boot_hart,
		.phandle = fdt_get_phandle(fdt, node),
	};
	name = fdt_get_name(fdt, node, NULL);
	memcpy(dom->name, name, strnlen(name, sizeof(dom->name) - 1));

	/* Its own regions, and maybe all of the root domain's. */
	list = fdt_getprop(fdt, node, "regions", &len);
	regions_alloc(dom, 1 + (list ? (unsigned int)len / 8 : 0));

	list = fdt_getprop(fdt, node, "possible-harts", &len);
	for (int i = 0; list && i < len / 4; i++) {
		int idx = cpu_hart_index(fdt, fdt32_to_cpu(list[i]));

		if (idx >= 0)
			hartmask_set(&dom->possible, (unsigned int)idx);
	}

	/*
	 * What is the monitor's is out of every domain's reach as it is
	 * ("m-only", the default); "all" is the rest of the root domain too,
	 * for the regions below to take away from.
	 */
	inherit = fdt_getprop(fdt, node, "root-regions-inheritance", NULL);
	if (inherit && !strcmp(inherit, "all"))
		add_region(dom, 0, __RISCV_XLEN__, DOMAIN_PERM_SU_RWX, false);

	/* <&memregion permissions> pairs. */
	list = fdt_getprop(fdt, node, "regions", &len);
	for (int i = 0; list && i + 1 < len / 4; i += 2) {
		uint32_t phandle = fdt32_to_cpu(list[i]);
		int r = fdt_node_offset_by_phandle(fdt, phandle);
		uint64_t base = 0;
		uint32_t order = 0;

		if (r < 0 ||
		    fdt_node_check_compatible(fdt, r, "riscv,domain,memregion"))
			return -1;
		base = prop_u64(fdt, r, "base", &given);
		order = fdt_prop_u32(fdt, r, "order", 0);
		if (!given || order < 3 || order > __RISCV_XLEN__ ||
		    (order < __RISCV_XLEN__ &&
		     ((base >> 1 >> (__RISCV_XLEN__ - 1)) ||
		      !IS_ALIGNED(base, BIT64(order)))) ||
		    add_region(dom, (unsigned long)base, order,
			       fdt32_to_cpu(list[i + 1]),
			       fdt_getprop(fdt, r, "mmio", NULL)))
			return -1;
	}

	list = fdt_getprop(fdt, node, "boot-hart", &len);
	if (list && len >= 4)
		dom->boot_hart = cpu_hart_index(fdt, fdt32_to_cpu(*list));

	dom->next_addr =
		(unsigned long)prop_u64(fdt, node, "next-addr", &given);
	dom->next_arg1 = (unsigned long)prop_u64(fdt, node, "next-arg1",
						 &dom->next_arg1_given);
	dom->next_mode = fdt_prop_u32(fdt, node, "next-mode", PRV_S) ? PRV_S :
								       PRV_U;
	dom->reset_allowed =
		fdt_getprop(fdt, node, "system-reset-allowed", NULL);
	dom->suspend_allowed =
		fdt_getprop(fdt, node, "system-suspend-allowed", NULL);
	return 0;
}

/* "riscv,domain" of the cpu nodes: those harts leave the root domain. */
static void assign_harts(const void *fdt)
{
	int cpus = fdt_path_offset(fdt, "/cpus"), cpu = 0;
	uint64_t hartid = 0;

	fdt_for_each_subnode(cpu, fdt, cpus) {
		uint32_t phandle = fdt_prop_u32(fdt, cpu, "riscv,domain", 0);
		struct domain *dom = NULL;
		int idx = 0;

		if (!phandle || fdt_reg(fdt, cpu, 0, &hartid, NULL))
			continue;
		idx = hart_index((unsigned long)hartid);
		if (idx < 0)
			continue;
		for (unsigned int d = 1; d < nr_domains; d++)
			if (domains[d].phandle == phandle)
				dom = &domains[d];
		if (!dom || !hartmask_test(&dom->possible, (unsigned int)idx)) {
			pr_warn("domain: hart %lu stays in the root domain (%s)\n",
				(unsigned long)hartid,
				dom ? "not a possible hart of its domain" :
				"no such domain");
			continue;
		}
		hartmask_clear(&domains[0].assigned, (unsigned int)idx);
		hartmask_set(&dom->assigned, (unsigned int)idx);
		hart_by_index((unsigned int)idx)->domain = dom;
	}
}

void domains_init(const void *fdt, unsigned long next_addr,
		  unsigned long next_mode)
{
	unsigned int instances = 0;
	int config = 0, node = 0, len = 0;

	/* As many as the tree has. */
	config = fdt ? fdt_node_offset_by_compatible(fdt, -1,
						     "riscv,domain,config") :
		       -1;
	if (config >= 0)
		fdt_for_each_subnode(node, fdt, config)
			instances += instance_node(fdt, node);
	domains = heap_alloc_array(1 + instances, sizeof(*domains));
	root_domain_init(next_addr, next_mode);
	if (config < 0)
		return;

	fdt_for_each_subnode(node, fdt, config) {
		struct domain *dom = &domains[nr_domains];

		if (!instance_node(fdt, node))
			continue;
		if (instance_parse(fdt, node, dom)) {
			pr_warn("domain %s: ignored (bad node)\n",
				fdt_get_name(fdt, node, NULL));
			continue;
		}
		dom->index = nr_domains++;
	}
	/* Who may enter whom: once every domain has its index. */
	fdt_for_each_subnode(node, fdt, config) {
		struct domain *dom =
			domain_by_phandle(fdt_get_phandle(fdt, node));
		const fdt32_t *list = fdt_getprop(fdt, node,
						  "riscv,entry-allowed-from",
						  &len);

		if (!dom || !dom->index)
			continue;
		dom->entry_from = heap_alloc_array(bitstr_size(nr_domains),
						   sizeof(bitstr_t));
		for (int i = 0; list && i < len / 4; i++) {
			const struct domain *from =
				domain_by_phandle(fdt32_to_cpu(list[i]));

			if (from)
				bit_set(dom->entry_from, from->index);
			else
				pr_warn("domain %s: no such domain to be entered from\n",
					dom->name);
		}
	}
	assign_harts(fdt);

	/* The next stage the monitor was given is the boot hart's domain's. */
	if (!this_domain()->next_addr) {
		this_domain()->next_addr = next_addr;
		this_domain()->next_mode = next_mode;
	}
}

static void domain_print(const struct domain *dom)
{
	unsigned int i = 0;

	pr_info("domain %u: %s, harts", dom->index, dom->name);
	for_each_hart_in_mask(i, &dom->possible)
		pr_info(" %lu%s", hart_id_of(i),
			domain_hart_assigned(dom, i) ? "*" : "");
	pr_info(", next %lx (%c-mode)%s%s\n", dom->next_addr,
		dom->next_mode == PRV_S ? 'S' : 'U',
		dom->reset_allowed ? ", reset" : "",
		dom->suspend_allowed ? ", suspend" : "");
	for (i = 0; i < dom->nr_regions; i++) {
		const struct domain_region *r = &dom->regions[i];

		pr_info("  %lx-%lx %c%c%c%s\n", r->base, region_last(r),
			r->perm & DOMAIN_PERM_SU_R ? 'r' : '-',
			r->perm & DOMAIN_PERM_SU_W ? 'w' : '-',
			r->perm & DOMAIN_PERM_SU_X ? 'x' : '-',
			r->mmio ? " mmio" : "");
	}
}

void __noreturn domains_start(unsigned long fdt)
{
	struct domain *mine = this_domain();

	root_arg1 = fdt;
	for (unsigned int d = 0; d < nr_domains; d++) {
		struct domain *dom = &domains[d];

		if (!dom->next_arg1_given)
			dom->next_arg1 = root_arg1;
		/*
		 * A boot hart that has no S-mode cannot boot anything: the
		 * first hart of the domain that has one does it in its place.
		 */
		if (dom->boot_hart >= 0 &&
		    hart_by_index((unsigned int)dom->boot_hart)->no_smode) {
			dom->boot_hart = -1;
			for (unsigned int i = 0;
			     hart_by_index(i) && dom->boot_hart < 0; i++)
				if (domain_hart_assigned(dom, i) &&
				    hart_index_valid(i))
					dom->boot_hart = (int)i;
		}
		if (dom->nr_regions > pmp_domain_entries())
			panic("domain %s: %u regions, %u PMP entries left\n",
			      dom->name, dom->nr_regions, pmp_domain_entries());
		if (nr_domains > 1)
			domain_print(dom);
	}

	for (unsigned int d = 0; d < nr_domains; d++) {
		struct domain *dom = &domains[d];
		long rc = 0;

		/* A domain boots on its boot hart, if that hart is its own. */
		if ((dom == mine && dom->boot_hart == (int)this_hart_index()) ||
		    dom->boot_hart < 0 || !dom->next_addr ||
		    !domain_hart_assigned(dom, (unsigned int)dom->boot_hart))
			continue;
		if (dom == mine)
			pr_info("monitor: next stage at %lx (%c-mode) on hart %lu, arg1: %lx\n",
				dom->next_addr,
				dom->next_mode == PRV_S ? 'S' : 'U',
				hart_id_of((unsigned int)dom->boot_hart),
				dom->next_arg1);
		rc = domain_start(dom);
		if (rc)
			pr_warn("domain %s: cannot start (%ld)\n", dom->name,
				rc);
	}

	if (mine->boot_hart == (int)this_hart_index() &&
	    domain_range_ok(mine, mine->next_addr, 4, DOMAIN_PERM_SU_X)) {
		pr_info("monitor: next stage at %lx (%c-mode), arg1: %lx\n",
			mine->next_addr, mine->next_mode == PRV_S ? 'S' : 'U',
			mine->next_arg1);
		hsm_boot_hart_start(mine->next_addr, mine->next_arg1,
				    mine->next_mode);
	}
	pr_info("monitor: hart %lu boots no domain, waiting to be started\n",
		this_hartid());
	hsm_hart_wait();
}

/*
 * A domain that cannot touch a memory region has no use for the devices in it.
 */
static int disable_devices(void *fdt, const struct domain *dom)
{
	for (int i = 0;; i += 2) {
		for (int j = 0;; j++) {
			/*
			 * A node that changes moves the ones after it: look
			 * again.
			 */
			int inst =
				fdt_node_offset_by_phandle(fdt, dom->phandle);
			int len = 0, rlen = 0, r = 0, dev = 0, rc = 0;
			const fdt32_t *regions = NULL, *devices = NULL;
			uint32_t phandle = 0;

			regions = inst < 0 ? NULL :
					     fdt_getprop(fdt, inst, "regions",
							 &rlen);
			if (!regions || i + 1 >= rlen / 4)
				return 0;
			if (fdt32_to_cpu(regions[i + 1]) &
			    (DOMAIN_PERM_SU_R | DOMAIN_PERM_SU_W))
				break;
			phandle = fdt32_to_cpu(regions[i]);
			r = fdt_node_offset_by_phandle(fdt, phandle);
			devices = r < 0 ? NULL :
					  fdt_getprop(fdt, r, "devices", &len);
			if (!devices || j >= len / 4)
				break;
			phandle = fdt32_to_cpu(devices[j]);
			dev = fdt_node_offset_by_phandle(fdt, phandle);
			rc = dev < 0 ? 0 : fdt_node_disable(fdt, dev);
			if (rc)
				return rc;
		}
	}
}

int domains_fdt_fixup(void *fdt)
{
	const struct domain *dom = this_domain();
	int cpus = fdt_path_offset(fdt, "/cpus"), cpu = 0, node = 0, rc = 0;
	uint64_t hartid = 0;

	if (nr_domains == 1)
		return 0;

	/*
	 * Its harts: the ones it can run, less those another domain boots with.
	 */
	for (unsigned int i = 0; hart_by_index(i); i++) {
		if (hartmask_test(&dom->possible, i) &&
		    (domain_hart_assigned(dom, i) ||
		     domain_hart_assigned(&domains[0], i)))
			continue;
		fdt_for_each_subnode(cpu, fdt, cpus)
			if (!fdt_reg(fdt, cpu, 0, &hartid, NULL) &&
			    hartid == hart_id_of(i))
				break;
		if (cpu >= 0)
			rc = fdt_node_disable(fdt, cpu);
		if (rc)
			return rc;
	}
	if (dom->index)
		rc = disable_devices(fdt, dom);
	if (rc)
		return rc;

	/* Software inside a domain has no business with the partitioning. */
	fdt_for_each_subnode(cpu, fdt, cpus)
		fdt_nop_property(fdt, cpu, "riscv,domain");
	node = fdt_node_offset_by_compatible(fdt, -1, "riscv,domain,config");
	return node < 0 ? 0 : fdt_del_node(fdt, node);
}
