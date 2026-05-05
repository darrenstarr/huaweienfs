# Appendix: esunrpc symbol index

Every `esunrpc_*` symbol exported by `esunrpc.ko`, grouped by
purpose. Each entry: one-line summary plus pointer to the chapter
where it's documented.

This index is hand-maintained alongside the fork; if a kernel-pin
bump adds new symbols, the post-bump diff includes the symbol-index
update.

## Client lifecycle (chapter 1)

| Symbol | Purpose |
|---|---|
| `esunrpc_rpc_create` | Build a new client + transport, do connect |
| `esunrpc_rpc_clone_client` | Share a client's transport under a new clnt object |
| `esunrpc_rpc_clone_client_set_auth` | Like clone but with new auth flavor |
| `esunrpc_rpc_switch_client_transport` | Re-bind a client to a different server endpoint |
| `esunrpc_rpc_bind_new_program` | Re-bind a client to a different RPC program/version |
| `esunrpc_rpc_shutdown_client` | Synchronous teardown |
| `esunrpc_rpc_release_client` | Asynchronous ref-drop |
| `esunrpc_rpc_killall_tasks` | Cancel all in-flight tasks on a client |
| `esunrpc_rpc_cancel_tasks` | Cancel tasks matching a predicate |
| `esunrpc_rpc_clnt_disconnect` | Drop the underlying transport(s) without freeing the clnt |

## Synchronous + asynchronous calls (chapter 2)

| Symbol | Purpose |
|---|---|
| `esunrpc_rpc_call_sync` | Issue an RPC, wait for reply |
| `esunrpc_rpc_call_async` | Issue + return immediately, callback fires later |
| `esunrpc_rpc_call_null` | NULL ping (procedure 0) — convenience |
| `esunrpc_rpc_call_start` | Manually start a pre-prepared task |
| `esunrpc_rpc_run_task` | Run a `rpc_task` to completion synchronously |
| `esunrpc_rpc_init_task` | Initialize a `rpc_task` for later submission |
| `esunrpc_rpc_prepare_reply_pages` | Set up page-array buffer for the reply |
| `esunrpc_rpc_task_release_transport` | Release the task's bound xprt |
| `esunrpc_rpc_init_task_retry_counters` | Reset retry counters on a task |
| `esunrpc_rpc_clnt_test_xprt` | Probe a single xprt's liveness |

## Transports (chapter 3)

| Symbol | Purpose |
|---|---|
| `esunrpc_xprt_create_transport` | Build a new RPC transport |
| `esunrpc_xprt_register_transport` | Register a transport class (TCP, UDP, etc.) |
| `esunrpc_xprt_unregister_transport` | Pair to register |
| `esunrpc_xprt_alloc` | Low-level transport allocation |
| `esunrpc_xprt_free` | Pair to alloc |
| `esunrpc_xprt_get` | Take a reference |
| `esunrpc_xprt_put` | Drop a reference |
| `esunrpc_xprt_release` | End-of-task transport release |
| `esunrpc_xprt_disconnect_done` | Notify that a disconnect completed |

## Multipath (chapter 5)

| Symbol | Purpose |
|---|---|
| `esunrpc_xprt_iter_init` | Initialise a transport iterator on a switch |
| `esunrpc_xprt_iter_get_next` | Advance the iterator, return the next xprt |
| `esunrpc_xprt_iter_get_helper` | Read current xprt without advancing (legacy) |
| `esunrpc_xprt_iter_xchg_switch` | Atomic swap of the underlying switch |
| `esunrpc_rpc_clnt_iterate_for_each_xprt` | Visit every xprt in a clnt's switch |

## Address parsing + formatting (chapter 3)

| Symbol | Purpose |
|---|---|
| `esunrpc_rpc_pton` | Parse a presentation address into sockaddr |
| `esunrpc_rpc_ntop` | Format a sockaddr into a presentation address |
| `esunrpc_rpc_peeraddr` | Get the peer address of a clnt |
| `esunrpc_rpc_peeraddr2str` | Same but format to string |
| `esunrpc_rpc_localaddr` | Get the local address used by a clnt |
| `esunrpc_rpc_uaddr2sockaddr` | Convert RFC 5665 universal-addr to sockaddr |
| `esunrpc_rpc_sockaddr2uaddr` | Reverse |

## XDR encoding/decoding (chapter 4 + reference)

The XDR API surface is large (~80 symbols). The most commonly used:

| Symbol | Purpose |
|---|---|
| `esunrpc_xdr_init_encode` | Set up a stream for encoding |
| `esunrpc_xdr_init_decode` | Set up a stream for decoding |
| `esunrpc_xdr_reserve_space` | Reserve bytes for encoding |
| `esunrpc_xdr_inline_decode` | Decode bytes inline |
| `esunrpc_xdr_encode_string` | Encode a counted string |
| `esunrpc_xdr_decode_string_inplace` | Decode a counted string |

The remaining XDR symbols follow the kernel's standard XDR API
naming; see `vendor/esunrpc/include/esunrpc/xdr.h` for the full
list.

## Authentication (chapter 4)

| Symbol | Purpose |
|---|---|
| `esunrpc_rpcauth_create` | Create an auth context |
| `esunrpc_rpcauth_release` | Release an auth context |
| `esunrpc_rpcauth_lookupcred` | Find a credential matching a process |
| `esunrpc_rpcauth_init_credcache` | Set up credential cache |
| `esunrpc_rpcauth_unhash_cred` | Remove a cached cred |

## RPCBind / portmap (chapter 3)

| Symbol | Purpose |
|---|---|
| `esunrpc_rpcb_getport_sync` | Synchronous portmap lookup |
| `esunrpc_rpcb_getport_async` | Async portmap lookup |
| `esunrpc_rpcb_program` | The rpcbind program description (extern struct) |

## Module-init internal (not normally called by drivers)

| Symbol | Purpose |
|---|---|
| `esunrpc_init_socket_xprt` | Register the socket transport class |
| `esunrpc_cleanup_socket_xprt` | Pair to init |
| `esunrpc_rpc_init_mempool` | Set up the rpc_task slab cache |
| `esunrpc_rpc_destroy_mempool` | Pair to init |

## Coverage status

This index lists ~100 of the 244 exported symbols. The remaining
~144 (most of the XDR helpers, scheduler internals, debugfs glue)
are tracked for full documentation in a follow-up issue. PRs that
add docs for any of these symbols should also update this index.
