// SPDX-License-Identifier: GPL-2.0-only
/*
 * Server-side helper stubs for the client-only esunrpc.ko fork.
 *
 * sysctl.c calls svc_print_xprts() to render the list of registered
 * server transports for /proc. esunrpc has no server side so the
 * "registered transports" list is permanently empty. The stub
 * returns 0 (zero bytes written) which sysctl.c handles correctly.
 *
 * Add other server stubs here as they show up at modpost time.
 */
#include <linux/types.h>
#include <linux/export.h>

int svc_print_xprts(char *buf, int maxlen)
{
	(void)buf;
	(void)maxlen;
	return 0;
}
EXPORT_SYMBOL_GPL(svc_print_xprts);
