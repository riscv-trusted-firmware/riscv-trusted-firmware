// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

#include <driver.h>
#include <log.h>

void drivers_init(void)
{
	const struct driver *d = NULL;

	for_each_driver(d) {
		int rc = d->probe ? d->probe() : 0;

		if (rc)
			pr_warn("driver %s: probe failed (%d)\n", d->name, rc);
		else
			pr_dbg("driver %s: ok\n", d->name);
	}
}
