% ESUNRPC_RPC_SHUTDOWN_CLIENT(3) | enfs-dkms manual
% enfs-dkms maintainers
% May 2026

# NAME

esunrpc_rpc_shutdown_client - synchronous teardown of an esunrpc client

# SYNOPSIS

**#include <esunrpc/clnt.h>**

**void esunrpc_rpc_shutdown_client(struct esunrpc_rpc_clnt \***\ *clnt*\ **);**

# DESCRIPTION

**esunrpc_rpc_shutdown_client**() cancels every in-flight task on
*clnt*, waits for them to complete (with **-ESHUTDOWN**), then
drops the caller's reference. If that drop brings the refcount to
zero, the underlying transport is disconnected and the client is
freed.

Unlike **esunrpc_rpc_release_client**(3), this function blocks
until pending callbacks have actually returned. That guarantee is
load-bearing when the caller is unloading the module that owns
those callbacks: by the time **shutdown** returns, no more
callback functions will fire, so the module can safely be
unmapped from the kernel.

# RETURN VALUE

None.

# CONTEXT

Sleeps. Process context only. Must NOT be called from interrupt
or RCU read-side.

# LIFETIME

After **shutdown** returns, *clnt* is invalid; do not dereference
it. The caller is also responsible for ensuring no other thread
holds a reference (via clone) that would keep the underlying
transport alive — see **esunrpc_rpc_clone_client**(3).

# SEE ALSO

**esunrpc_rpc_create**(3),
**esunrpc_rpc_release_client**(3),
**esunrpc_rpc_clone_client**(3),
**esunrpc_rpc_killall_tasks**(3),
**esunrpc**(7).
