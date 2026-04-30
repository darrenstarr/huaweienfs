# Top-level Kbuild for the enfs out-of-tree build.
#
# Invoked as:  make -C /lib/modules/$KVER/build M=$PWD/src modules
#
# Produces three modules into the directories that mirror an in-tree build:
#   src/net/sunrpc/sunrpc.ko    (stock SunRPC + sunrpc_enfs_adapter.o)
#   src/fs/nfs/nfs.ko           (stock NFS    + enfs_adapter.o)
#   src/fs/nfs/enfs/enfs.ko     (the new multipath module)
#
# The `port` step (scripts/apply-compat-shims.sh) is responsible for
# materialising src/ from vendor/openeuler/ + compat/ + patches/ before
# this Kbuild is invoked. Until that script lands, this file describes
# the intended structure but will not produce modules.

# Kbuild walks subdirectories that end in '/'; an obj-y entry pointing at
# a directory triggers descent into it.
obj-y += net/sunrpc/
obj-y += fs/nfs/
obj-y += fs/nfs/enfs/

# Make every translation unit see compat/ first so the kernel-version
# shims override stock symbols without having to touch the vendored .c
# files line-by-line.
ccflags-y += -I$(src)/compat
ccflags-y += -DCONFIG_ENFS=1 -DCONFIG_SUNRPC_ENFS=1
