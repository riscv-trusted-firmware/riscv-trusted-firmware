// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * QEMU virt machine.
 */

#include <drivers/serial/uart8250.h>
#include <log.h>
#include <mpxy.h>
#include <platform.h>
#include <rpmi.h>

void plat_early_init(void)
{
	uart8250_console_init();
}

void plat_init(void)
{
#if defined(IMAGE_MONITOR) && defined(CONFIG_QEMU_VIRT_RPMI)
	/*
	 * No platform microcontroller here: the channels lead to whoever
	 * serves the shared memory queues (the PuC model of the SBI test
	 * payload). Voltage is there to be probed and found missing.
	 */
	if (mpxy_rpmi_channel_add(CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE,
				  RPMI_GROUP_CLOCK) ||
	    mpxy_rpmi_channel_add(CONFIG_QEMU_VIRT_RPMI_CHANNEL_BASE + 1,
				  RPMI_GROUP_VOLTAGE))
		pr_warn("qemu-virt: cannot add the RPMI channels\n");
#endif
}
