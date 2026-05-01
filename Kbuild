# Top-level Kbuild for the enfs out-of-tree build.
#
# Invoked as:  make -C /lib/modules/$KVER/build M=$PWD/src modules
#
# Produces three modules into directories that mirror an in-tree build:
#   net/sunrpc/sunrpc.ko   (stock SunRPC + sunrpc_enfs_adapter.o)
#   fs/nfs/nfs.ko          (stock NFS    + enfs_adapter.o)
#   fs/nfs/enfs/enfs.ko    (the new multipath module)
#
# This file is what scripts/build-src-tree.sh copies into src/Kbuild
# so the kernel build system sees it when invoked with M=src.
#
# We list every .o explicitly here rather than relying on the
# per-subdir Makefiles' `obj-$(CONFIG_X)` lines, because those depend
# on Make-time variables (CONFIG_NFS_FS, CONFIG_SUNRPC, ...) that are
# *not* set when building out-of-tree against linux-headers-*.

# Our patched headers under src/include/ MUST be searched before the
# kernel's linux-headers copies. ccflags-y -I lands AFTER LINUXINCLUDE
# in the compile command, so we prepend to LINUXINCLUDE itself.
# This way <linux/sunrpc/sched.h> (and our other patched headers)
# resolves to our copy. The new <linux/sunrpc/sunrpc_enfs_adapter.h>
# also lives there.
override LINUXINCLUDE := -I$(src)/include $(LINUXINCLUDE)

ccflags-y += -I$(src)/compat
# Force-include enfs_compat.h in every TU so symbols added there
# (e.g., NFSDBG_ENFS) are visible without requiring an explicit
# #include in patched/vendored sources.
ccflags-y += -include $(src)/compat/enfs_compat.h

# Force the kernel-config #defines that the in-tree build would set.
# These guard most of the conditional blocks in the patched files;
# without them, optional fields disappear from struct nfs_client etc.
ccflags-y += -DCONFIG_ENFS=1
ccflags-y += -DCONFIG_SUNRPC_ENFS=1
ccflags-y += -DCONFIG_NFS_LOCALIO=1
ccflags-y += -DCONFIG_NFS_FSCACHE=1
ccflags-y += -DCONFIG_NFS_V3=1
ccflags-y += -DCONFIG_NFS_V3_ACL=1
ccflags-y += -DCONFIG_NFS_V4=1
ccflags-y += -DCONFIG_NFS_V4_1=1
ccflags-y += -DCONFIG_NFS_V4_2=1
ccflags-y += -DCONFIG_SUNRPC_BACKCHANNEL=1
ccflags-y += -DCONFIG_SUNRPC_DEBUG=1

# ---------------------------------------------------------------------
# sunrpc.ko — stock SunRPC + sunrpc_enfs_adapter.o
# Object list mirrors net/sunrpc/Makefile (sunrpc-y plus the *_DEBUG /
# *_BACKCHANNEL / *_PROC_FS / *_SYSCTL conditionals, which we treat as
# always-on for our build).
# ---------------------------------------------------------------------
obj-m += sunrpc.o
sunrpc-y := \
	net/sunrpc/clnt.o net/sunrpc/xprt.o net/sunrpc/socklib.o \
	net/sunrpc/xprtsock.o net/sunrpc/sched.o \
	net/sunrpc/auth.o net/sunrpc/auth_null.o net/sunrpc/auth_tls.o \
	net/sunrpc/auth_unix.o \
	net/sunrpc/svc.o net/sunrpc/svcsock.o \
	net/sunrpc/svcauth.o net/sunrpc/svcauth_unix.o \
	net/sunrpc/addr.o net/sunrpc/rpcb_clnt.o \
	net/sunrpc/timer.o net/sunrpc/xdr.o \
	net/sunrpc/sunrpc_syms.o net/sunrpc/cache.o net/sunrpc/rpc_pipe.o \
	net/sunrpc/sysfs.o net/sunrpc/svc_xprt.o \
	net/sunrpc/xprtmultipath.o \
	net/sunrpc/debugfs.o net/sunrpc/backchannel_rqst.o \
	net/sunrpc/stats.o net/sunrpc/sysctl.o \
	net/sunrpc/sunrpc_enfs_adapter.o

# ---------------------------------------------------------------------
# nfs.ko — stock NFS client + enfs_adapter.o
# Mirrors fs/nfs/Makefile's nfs-y plus the FSCACHE / LOCALIO / ROOT_NFS
# / SYSCTL conditionals (we enable them).
# ---------------------------------------------------------------------
obj-m += nfs.o
nfs-y := \
	fs/nfs/client.o fs/nfs/dir.o fs/nfs/file.o fs/nfs/getroot.o \
	fs/nfs/inode.o fs/nfs/super.o fs/nfs/io.o fs/nfs/direct.o \
	fs/nfs/pagelist.o fs/nfs/read.o fs/nfs/symlink.o fs/nfs/unlink.o \
	fs/nfs/write.o fs/nfs/namespace.o fs/nfs/mount_clnt.o \
	fs/nfs/nfstrace.o fs/nfs/export.o fs/nfs/sysfs.o \
	fs/nfs/fs_context.o \
	fs/nfs/nfsroot.o fs/nfs/sysctl.o fs/nfs/fscache.o \
	fs/nfs/localio.o \
	fs/nfs/enfs_adapter.o

# Per the in-tree Makefile, fs/nfs/nfstrace.c needs an -I to the dir
# holding nfstrace.h so the kernel's trace-event mechanism finds the
# event-defining header. In the in-tree build that's `CFLAGS_nfstrace.o
# += -I$(src)` evaluated in fs/nfs/Makefile (where $(src) is fs/nfs/).
# Our top-level Kbuild sees $(src) = src/, so spell out the path.
# Also flag the same for nfs4trace.o (NFSv4 has its own trace header,
# even though we don't currently build it as an object — preserved for
# future when we add NFSv4 multipath).
CFLAGS_fs/nfs/nfstrace.o += -I$(src)/fs/nfs
CFLAGS_fs/nfs/nfs4trace.o += -I$(src)/fs/nfs

# ---------------------------------------------------------------------
# enfs.ko — the standalone multipath module from vendor/openeuler/.
# Object list mirrors vendor/openeuler/fs/nfs/enfs/Makefile.
# ---------------------------------------------------------------------
obj-m += fs/nfs/enfs/enfs.o
fs/nfs/enfs/enfs-y := \
	fs/nfs/enfs/enfs_init.o \
	fs/nfs/enfs/enfs_config.o \
	fs/nfs/enfs/mgmt_init.o \
	fs/nfs/enfs/enfs_multipath_client.o \
	fs/nfs/enfs/enfs_multipath_parse.o \
	fs/nfs/enfs/failover_path.o \
	fs/nfs/enfs/failover_time.o \
	fs/nfs/enfs/enfs_roundrobin.o \
	fs/nfs/enfs/enfs_multipath.o \
	fs/nfs/enfs/enfs_path.o \
	fs/nfs/enfs/enfs_proc.o \
	fs/nfs/enfs/enfs_remount.o \
	fs/nfs/enfs/pm_ping.o \
	fs/nfs/enfs/pm_state.o \
	fs/nfs/enfs/enfs_rpc_init.o \
	fs/nfs/enfs/enfs_rpc_proc.o \
	fs/nfs/enfs/exten_call.o \
	fs/nfs/enfs/shard_route.o \
	fs/nfs/enfs/dns_process.o \
	fs/nfs/enfs/enfs_lookup_cache.o
