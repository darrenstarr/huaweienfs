% ESUNRPC(7) | enfs-dkms manual
% enfs-dkms maintainers
% May 2026

# NAME

esunrpc - forked client-side SunRPC for the enfs project

# SYNOPSIS

**modprobe esunrpc**

# DESCRIPTION

**esunrpc** is a forked, renamed copy of the Linux kernel's
client-side SunRPC subsystem. It exists so the enfs multipath NFS
project can ship a clean stack to vanilla kernels (debian, etc.)
without overriding stock **sunrpc.ko**. Both modules coexist at
runtime: every exported symbol, **/proc** path, **/sys** kset,
filesystem name, slab cache, workqueue, and debugfs directory in
**esunrpc** is renamed with an **esunrpc** prefix to avoid
collision.

Currently this module is the foundation for follow-up work — it
loads cleanly and exports its renamed symbols, but no in-kernel
caller is yet wired up to use it. The companion **enfs_nfsv3.ko**
module (a forked NFSv3 client that depends on **esunrpc**) lands
in a follow-up PR; see *docs/internals/15-esunrpc-fork.md* §15.8
for the multi-PR roadmap.

# RUNTIME OBSERVABILITY

After loading **esunrpc**, the following userspace surfaces appear
alongside the stock SunRPC equivalents:

*/proc/net/esunrpc/*
:   per-net **esunrpc** statistics (mirrors */proc/net/rpc/*).

*/sys/kernel/esunrpc/*
:   per-client and per-transport sysfs objects.

*/sys/module/esunrpc/parameters/*
:   currently empty (no module parameters yet).

The stock **/proc/net/rpc/** and **/sys/kernel/sunrpc/** continue
to be owned by stock **sunrpc.ko** and are unaffected.

# SYMBOL NAMESPACE

All exported symbols use the **esunrpc_** prefix.
**esunrpc_rpc_create** is the **esunrpc** equivalent of the
kernel's **rpc_create**, and so on. There are 244 such exports;
see **esunrpc_rpc_create**(3) and the symbol-index appendix at
*docs/esunrpc-api/appendix-symbol-index.md* for the full list.

# CALLER GUIDE

For step-by-step examples, including a minimum-viable hello-world
client, see *docs/esunrpc-api/0-getting-started.md*. The full
programmer's reference is in *docs/esunrpc-api/*.

# FILES

*/lib/modules/$(uname -r)/updates/esunrpc.ko*
:   the module installed by the enfs-dkms package.

*/proc/net/esunrpc/*
:   per-net statistics.

*/sys/kernel/esunrpc/*
:   per-client + per-transport sysfs.

# SEE ALSO

**esunrpc_rpc_create**(3),
**esunrpc_rpc_call_sync**(3),
**esunrpc_rpc_shutdown_client**(3),
**enfs**(7),
**rpcdebug**(8),
**rpc.idmapd**(8).

# COLOPHON

This page is part of the enfs-dkms project, the multipath NFS
client port from OpenEuler to Ubuntu LTS. Project documentation
lives at *docs/* in the source tree; the internals book that
covers the **esunrpc** fork specifically is
*docs/internals/15-esunrpc-fork.md*.
