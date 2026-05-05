# Getting started with esunrpc

A minimum-viable example: load `esunrpc.ko`, create a client to
talk to a local rpcbind on `127.0.0.1`, call NULL, and tear down.
Five steps, ~25 lines of kernel-module code.

## 0. Prerequisites

- `esunrpc.ko` loaded: `sudo modprobe esunrpc`
- A user-mode RPC server you can talk to. The simplest thing is
  the local rpcbind (port 111) which always answers `NULL` on the
  RPCBIND/PMAP program, and which is already running on any host
  with `nfs-common` installed.
- Kernel module build environment with `linux-headers-$(uname -r)`.

## 1. The example module

```c
/* esunrpc-hello.c — talk to local rpcbind, call NULL, exit. */
#include <linux/module.h>
#include <linux/in.h>
#include <esunrpc/clnt.h>
#include <esunrpc/sched.h>

#define RPCB_PROGRAM    100000
#define RPCB_VERSION    2

static int __init hello_init(void)
{
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(111),
        .sin_addr   = { .s_addr = htonl(INADDR_LOOPBACK) },
    };
    struct esunrpc_rpc_create_args args = {
        .net        = &init_net,
        .protocol   = IPPROTO_TCP,
        .address    = (struct sockaddr *)&srv,
        .addrsize   = sizeof(srv),
        .servername = "rpcbind",
        .program    = &esunrpc_rpcb_program,  /* see vendor/esunrpc/net/esunrpc/rpcb_clnt.c */
        .version    = RPCB_VERSION,
        .authflavor = RPC_AUTH_NULL,
    };

    struct esunrpc_rpc_clnt *clnt = esunrpc_rpc_create(&args);
    if (IS_ERR(clnt)) {
        pr_err("esunrpc-hello: create failed: %ld\n", PTR_ERR(clnt));
        return PTR_ERR(clnt);
    }

    int err = esunrpc_rpc_call_null(clnt, NULL, 0);
    if (err)
        pr_err("esunrpc-hello: NULL call failed: %d\n", err);
    else
        pr_info("esunrpc-hello: NULL call OK\n");

    esunrpc_rpc_shutdown_client(clnt);
    return err;
}

static void __exit hello_exit(void) { }

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("esunrpc API hello-world");
```

(`esunrpc_rpc_create_args` is the name our fork uses for the struct
that stock sunrpc calls `rpc_create_args`. Same fields, different
namespace.)

## 2. Build it

```
obj-m += esunrpc-hello.o
ccflags-y += -I/path/to/enfs/src/include
```

```
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

## 3. Load + observe

```
sudo insmod ./esunrpc-hello.ko
dmesg | tail -1
# expected: esunrpc-hello: NULL call OK
sudo rmmod esunrpc-hello
```

If the call fails, check that rpcbind is running
(`systemctl status rpcbind`) and that the local socket is open
(`ss -tlnp | grep :111`).

## 4. What just happened

1. **`esunrpc_rpc_create()`** — built a `struct esunrpc_rpc_clnt`,
   allocated one transport, ran the connect handshake
   synchronously. The clnt is reference-counted; you own one ref
   until you call `esunrpc_rpc_shutdown_client()`.
2. **`esunrpc_rpc_call_null()`** — convenience for "just ping
   it" — issues procedure 0 (NULL) on the bound program/version.
   Useful for liveness probes and for what we do here, smoke tests.
3. **`esunrpc_rpc_shutdown_client()`** — drops the last ref,
   tears down the transport, frees the clnt. After this call the
   `clnt` pointer is invalid.

## 5. Where to go next

- Read [chapter 1: clients](./1-clients.md) for the full lifecycle
  including cloning, transport switching, async ops.
- Read [chapter 5: multipath](./5-multipath.md) if your goal is
  multi-path NFS-style behavior — that's what this whole project
  is built around.
- The hand-written symbol reference is in
  [appendix: symbol index](./appendix-symbol-index.md).
