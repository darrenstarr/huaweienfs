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
# CONFIG_NFS_LOCALIO intentionally NOT defined: enabling it pulls in
# references to the nfslocalio module (nfs_uuid_init, nfs_local_doio,
# etc.) which lives outside our build. Without -D the related struct
# fields disappear and the references go away.
# CONFIG_NFS_FSCACHE intentionally NOT defined: needs fscache.ko +
# netfs symbols (__fscache_*) which are CRC-locked against stock
# sunrpc.ko's CRC of struct rpc_clnt — replacing sunrpc.ko forces
# fscache to be unloaded which may not be feasible. Defer to v1.
# CONFIG_NFS_V3_ACL intentionally NOT defined: needs nfs_acl.ko which
# is built against stock sunrpc CRCs.
# CONFIG_NFS_V4 paths that use lockd are similar — lockd.ko is built
# against stock CRCs. Until we vendor + rebuild lockd/nfs_acl (option
# (a) in the project plan), this build supports NFSv3 client multipath
# without ACLs or file locking.
ccflags-y += -DCONFIG_NFS_V3=1
ccflags-y += -DCONFIG_NFS_V3_ACL=1
ccflags-y += -DCONFIG_NFS_V4=1
ccflags-y += -DCONFIG_NFS_V4_1=1
ccflags-y += -DCONFIG_NFS_V4_2=1
ccflags-y += -DCONFIG_NFS_FSCACHE=1
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
	fs/nfs/fs_context.o fs/nfs/sysctl.o fs/nfs/fscache.o \
	fs/nfs/enfs_adapter.o
# Note: stock kernel builds nfs3xdr.o INTO nfsv3.ko, not nfs.ko.
# We do the same — nfs3_procedures[] is exported from nfsv3.ko (see
# below) and enfs.ko depends on nfsv3.ko at runtime.

# ---------------------------------------------------------------------
# nfsv3.ko — NFSv3 client.
# Mirrors the stock fs/nfs/Makefile nfsv3-y line (minus ACL — we keep
# CONFIG_NFS_V3_ACL effectively off since rebuilding the whole ACL
# story isn't strictly needed for the multipath e2e test).
# ---------------------------------------------------------------------
obj-m += fs/nfs/nfsv3.o
fs/nfs/nfsv3-y := \
	fs/nfs/nfs3super.o fs/nfs/nfs3client.o fs/nfs/nfs3proc.o \
	fs/nfs/nfs3xdr.o fs/nfs/nfs3acl.o
# Dropped from v0 (optional features that pull in symbols not exported
# by stock Ubuntu sunrpc/nfslocalio):
#   - fs/nfs/nfsroot.o  (NFS-on-root needs root_server_addr/path)
#   - fs/nfs/localio.o  (localio fast-path needs the nfslocalio module)
# Re-add them once the corresponding symbols are exported (would need
# additional patches to fs/nfs/nfsroot or to vendor/import nfslocalio).

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
# lockd.ko — stock NLM lock manager. Rebuilt against our patched
# sunrpc.ko so its CRCs line up. nfs.ko depends on this for byte-range
# locking (nlmclnt_*).
# ---------------------------------------------------------------------
obj-m += fs/lockd/lockd.o
fs/lockd/lockd-y := \
	fs/lockd/clntlock.o fs/lockd/clntproc.o fs/lockd/clntxdr.o \
	fs/lockd/host.o fs/lockd/svc.o fs/lockd/svclock.o \
	fs/lockd/svcshare.o fs/lockd/svcproc.o fs/lockd/svcsubs.o \
	fs/lockd/mon.o fs/lockd/trace.o fs/lockd/xdr.o \
	fs/lockd/clnt4xdr.o fs/lockd/xdr4.o fs/lockd/svc4proc.o \
	fs/lockd/procfs.o
# fs/lockd/netlink.c was added in Ubuntu 7.0 (NLM v4 netlink config
# interface). Older kernels (6.8, 6.11) don't have it. Pick it up
# conditionally so the same Kbuild works across vendored targets.
fs/lockd/lockd-y += $(if $(wildcard $(src)/fs/lockd/netlink.c),fs/lockd/netlink.o)
CFLAGS_fs/lockd/trace.o += -I$(src)/fs/lockd

# ---------------------------------------------------------------------
# nfs_acl.ko — NFSv3 ACL XDR (also rebuilt for CRC alignment).
# ---------------------------------------------------------------------
obj-m += fs/nfs_common/nfs_acl.o
fs/nfs_common/nfs_acl-y := fs/nfs_common/nfsacl.o

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
	fs/nfs/enfs/dns_process.o \
	fs/nfs/enfs/enfs_lookup_cache.o
