// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, The RISC-V Trusted Firmware contributors
 */

/*
 * RISC-V ACLINT MSWI (and the software interrupt half of the SiFive
 * CLINT): one MSIP register per hart.
 */

#include <driver.h>
#include <io.h>
#include <ipi.h>
#include <stdint.h>
#include <types_ext.h>

static vaddr_t msip(unsigned long hartid)
{
	return CONFIG_IPI_ACLINT_MSWI_ADDR +
	       4 * (hartid - CONFIG_IPI_ACLINT_MSWI_FIRST_HART);
}

static void aclint_mswi_send(unsigned long hartid)
{
	io_write32(msip(hartid), 1);
}

static void aclint_mswi_clear(unsigned long hartid)
{
	io_write32(msip(hartid), 0);
}

static const struct ipi_ops aclint_mswi_ops = {
	.name = "aclint-mswi",
	.send = aclint_mswi_send,
	.clear = aclint_mswi_clear,
};

static int aclint_mswi_probe(void)
{
	ipi_register(&aclint_mswi_ops);
	return 0;
}

DRIVER_DEFINE(aclint_mswi) = {
	.name = "aclint-mswi",
	.probe = aclint_mswi_probe,
};
