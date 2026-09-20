// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * QEMU virt machine. QEMU describes the machine in the device tree it
 * passes; there is nothing board-specific left to do beyond choosing the
 * console driver.
 */

#include <drivers/serial/uart8250.h>
#include <fdt_util.h>
#include <platform.h>
#include <util.h>

void plat_early_init(const void *fdt)
{
	uart8250_console_init(fdt);
}

void plat_init(void)
{
}

#if defined(IMAGE_MONITOR) && defined(CONFIG_QEMU_VIRT_RPMI)

#include <rpmi.h>

/*
 * QEMU virt has no platform microcontroller, and says nothing about one.
 * For the SBI test payload's PuC model, describe an RPMI shared memory
 * transport in RAM, MPXY channels for the clock and voltage groups (the
 * latter is there to be probed and found missing) and the M-mode users of
 * the transport, the way a platform with a PuC would.
 */
#define SHMEM_BASE CONFIG_QEMU_VIRT_RPMI_SHMEM_BASE
#define QUEUE_SIZE CONFIG_QEMU_VIRT_RPMI_QUEUE_SIZE

static int add_rpmi_user(void *fdt, int parent, const char *name,
			 const char *compatible, uint32_t transport,
			 uint32_t group, uint32_t channel)
{
	int node = fdt_add_subnode(fdt, parent, name), rc = 0;
	fdt32_t mboxes[2] = { cpu_to_fdt32(transport), cpu_to_fdt32(group) };

	if (node < 0)
		return node;
	rc = fdt_setprop(fdt, node, "compatible", compatible,
			 (int)strlen(compatible) + 1);
	if (!rc)
		rc = fdt_setprop(fdt, node, "mboxes", mboxes, sizeof(mboxes));
	if (!rc && channel)
		rc = fdt_setprop_u32(fdt, node, "riscv,sbi-mpxy-channel-id",
				     channel);
	return rc;
}

int plat_fdt_prepare(void *fdt)
{
	static const char mbox_compatible[] = "riscv,rpmi-shmem-mbox";
	static const char names[] = "a2p-req\0p2a-ack\0p2a-req\0a2p-ack";
	const uint32_t channel = CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE;
	int soc = fdt_path_offset(fdt, "/soc"), node = 0, ac = 0, sc = 0,
	    rc = 0;
	fdt32_t reg[4 * 4] = {}, *p = reg;
	uint32_t phandle = 0;

	if (soc < 0)
		return soc;
	ac = fdt_address_cells(fdt, soc);
	sc = fdt_size_cells(fdt, soc);
	if (ac < 1 || ac > 2 || sc < 1 || sc > 2)
		return -FDT_ERR_BADNCELLS;
	rc = fdt_generate_phandle(fdt, &phandle);
	if (rc)
		return rc;

	for (unsigned int q = 0; q < RPMI_QUEUE_COUNT; q++) {
		uint64_t base = SHMEM_BASE + (uint64_t)q * QUEUE_SIZE;

		if (ac == 2)
			*p++ = cpu_to_fdt32(high32_from_64(base));
		*p++ = cpu_to_fdt32((uint32_t)base);
		if (sc == 2)
			*p++ = cpu_to_fdt32(0);
		*p++ = cpu_to_fdt32(QUEUE_SIZE);
	}

	node = fdt_add_subnode(fdt, soc, "rpmi-mailbox");
	if (node < 0)
		return node;
	rc = fdt_setprop(fdt, node, "compatible", mbox_compatible,
			 sizeof(mbox_compatible));
	if (!rc)
		rc = fdt_setprop(fdt, node, "reg", reg,
				 (int)((char *)p - (char *)reg));
	if (!rc)
		rc = fdt_setprop(fdt, node, "reg-names", names, sizeof(names));
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "riscv,slot-size",
				     CONFIG_QEMU_VIRT_RPMI_SLOT_SIZE);
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "#mbox-cells", 1);
	if (!rc)
		rc = fdt_setprop_u32(fdt, node, "phandle", phandle);

		/*
		 * Offsets move as nodes are added: look /soc up again each
		 * time.
		 */
#define USER(name, compatible, group, chan)                                   \
	do {                                                                  \
		if (!rc)                                                      \
			rc = add_rpmi_user(fdt, fdt_path_offset(fdt, "/soc"), \
					   name, compatible, phandle, group,  \
					   chan);                             \
	} while (0)
	USER("rpmi-clock", "riscv,rpmi-mpxy-clock", RPMI_GROUP_CLOCK, channel);
	USER("rpmi-voltage", "riscv,rpmi-mpxy-voltage", RPMI_GROUP_VOLTAGE,
	     channel + 1);
	USER("rpmi-system-reset", "riscv,rpmi-system-reset",
	     RPMI_GROUP_SYSTEM_RESET, 0);
	USER("rpmi-system-suspend", "riscv,rpmi-system-suspend",
	     RPMI_GROUP_SYSTEM_SUSPEND, 0);
	USER("rpmi-hsm", "riscv,rpmi-hsm", RPMI_GROUP_HSM, 0);
	USER("rpmi-cppc", "riscv,rpmi-cppc", RPMI_GROUP_CPPC, 0);
#undef USER
	return rc;
}

#else

int plat_fdt_prepare(void *fdt)
{
	return 0;
}

#endif
