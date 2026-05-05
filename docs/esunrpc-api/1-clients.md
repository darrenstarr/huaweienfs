# esunrpc clients — lifecycle and API

A `struct esunrpc_rpc_clnt` represents a bound RPC program/version
on a particular server endpoint. It owns at least one transport
(multipath: more than one) and an authentication context.

## esunrpc_rpc_create — create a client

**Signature:**

    struct esunrpc_rpc_clnt *
    esunrpc_rpc_create(struct esunrpc_rpc_create_args *args);

**Purpose**

Allocate a new client, build its transport, run the connect
handshake (synchronously by default), and return a usable client
object. This is the only way to make a `esunrpc_rpc_clnt` — there
is no "construct empty, fill in later" path; every required field
is in `args`.

**Arguments**

- `args` — must be non-NULL. The fields the caller MUST set:
  - `net` — network namespace (usually `&init_net`)
  - `protocol` — `IPPROTO_TCP` or `IPPROTO_UDP`
  - `address` + `addrsize` — server endpoint
  - `servername` — printable string for diagnostics
  - `program` — pointer to the `struct rpc_program` describing
    the RPC program (e.g. `esunrpc_rpcb_program` for rpcbind)
  - `version` — protocol minor version
  - `authflavor` — usually `RPC_AUTH_NULL` or `RPC_AUTH_UNIX`

  Optional but commonly set:
  - `flags` — `RPC_CLNT_CREATE_DISCRTRY` etc.; usually 0
  - `timeout` — overrides the program's default RTO

**Returns**

A pointer to the new client on success; an `ERR_PTR()` on failure.
Use `IS_ERR()` to check, `PTR_ERR()` to extract the errno. Common
errnos:

- `-EINVAL` — args invalid (missing required field, bad family)
- `-ECONNREFUSED` — connect to server failed
- `-EHOSTUNREACH` — no route to server
- `-ENOMEM` — allocation failed

**Locking and context**

Sleeps. Must be called from process context. Caller holds no
locks before invocation; the function takes whatever it needs
internally.

**Threading**

Reentrant — multiple cores may call concurrently with different
`args`. Each call produces an independent client.

**Lifetime**

The returned client comes with one reference owned by the caller.
Drop it via `esunrpc_rpc_shutdown_client()` (synchronous teardown
of the transport) or `esunrpc_rpc_release_client()` (just decrement
the ref; the actual destroy happens when the count hits zero).

After shutdown/release brings the count to zero, the pointer is
invalid; do not dereference.

**Example**

    struct sockaddr_in srv = { ... };
    struct esunrpc_rpc_create_args args = {
        .net        = &init_net,
        .protocol   = IPPROTO_TCP,
        .address    = (struct sockaddr *)&srv,
        .addrsize   = sizeof(srv),
        .servername = "my-server",
        .program    = &nfs_program,
        .version    = 3,
        .authflavor = RPC_AUTH_UNIX,
    };
    struct esunrpc_rpc_clnt *clnt = esunrpc_rpc_create(&args);
    if (IS_ERR(clnt)) {
        pr_err("create failed: %ld\n", PTR_ERR(clnt));
        return PTR_ERR(clnt);
    }
    /* ... use clnt ... */
    esunrpc_rpc_shutdown_client(clnt);

**See also**

`esunrpc_rpc_clone_client`, `esunrpc_rpc_shutdown_client`,
`esunrpc_rpc_release_client`, `esunrpc_rpc_bind_new_program`.

---

## esunrpc_rpc_clone_client — duplicate an existing client

**Signature:**

    struct esunrpc_rpc_clnt *
    esunrpc_rpc_clone_client(struct esunrpc_rpc_clnt *clnt);

**Purpose**

Returns a new client that shares the underlying transport(s) with
the original but has its own auth, retry policy, and per-task
state. Useful for issuing parallel requests to the same server
without serialising them through one client object.

**Arguments**

- `clnt` — must be non-NULL and currently held (refcount > 0).

**Returns**

A new clnt on success; `ERR_PTR(-ENOMEM)` on allocation failure.

**Lifetime**

The clone holds its own reference. Original ref unchanged. Both
clones must be released independently.

---

## esunrpc_rpc_shutdown_client — tear down synchronously

**Signature:**

    void esunrpc_rpc_shutdown_client(struct esunrpc_rpc_clnt *clnt);

**Purpose**

Cancels all in-flight tasks on the client, waits for them to
complete (with `-ESHUTDOWN`), then drops the caller's reference.
If that was the last ref, the transport is closed and the client
is freed.

The caller is guaranteed that on return, no new callbacks against
this client will fire — useful when the caller is unloading the
module that owns the callback functions.

**Arguments**

- `clnt` — must be non-NULL and currently held.

**Locking and context**

Sleeps. Must NOT be called from interrupt or RCU read-side.

**Lifetime**

After return, the pointer is invalid.

**See also**

`esunrpc_rpc_release_client` (asynchronous version — drops ref but
doesn't wait).
