% ESUNRPC_RPC_CALL_SYNC(3) | enfs-dkms manual
% enfs-dkms maintainers
% May 2026

# NAME

esunrpc_rpc_call_sync - issue an RPC and wait for the reply

# SYNOPSIS

**#include <esunrpc/clnt.h>**

**int esunrpc_rpc_call_sync(struct esunrpc_rpc_clnt \***\ *clnt*\ **,**
**const struct rpc_message \***\ *msg*\ **, int** *flags*\ **);**

# DESCRIPTION

**esunrpc_rpc_call_sync**() issues a single synchronous RPC on
*clnt* and blocks until the reply arrives, the server returns an
error, or the call exceeds the client's retry budget.

*msg* describes the procedure, arguments, and reply buffer:

**.rpc_proc**
:   pointer to the **struct rpc_procinfo** in the bound program's
    procedure table.

**.rpc_argp**
:   pointer to the encoded arguments (caller's struct).

**.rpc_resp**
:   pointer to where the decoded reply will be written.

**.rpc_cred**
:   credential to use; **NULL** to fall back to the client's
    default credential.

*flags* is a bitmask:

**RPC_TASK_ASYNC**
:   forbidden here — use **esunrpc_rpc_call_async**(3) instead.

**RPC_TASK_SOFT**
:   give up after the timeout instead of retrying forever.

**RPC_TASK_NOCONNECT**
:   fail immediately if the transport isn't already connected
    instead of triggering a re-connect.

# RETURN VALUE

Returns 0 on success.

On error, returns a negative errno:

**-EIO**
:   transport-level failure that exhausted the retry budget.

**-ETIMEDOUT**
:   only with **RPC_TASK_SOFT** — server did not respond in time.

**-ESHUTDOWN**
:   the client was being torn down while this call was pending.

**-EACCES**
:   authentication rejection (AUTH_BADCRED, AUTH_REJECTEDCRED).

Other negative errnos may be returned by the program-specific
decoder; consult the relevant program's documentation.

# CONTEXT

Sleeps. Process context only. The transport may be inactive when
the call is made; this function will trigger a connect.

# THREAD SAFETY

Reentrant. Multiple callers may invoke concurrently against the
same client; the underlying transport's slot table serialises
on-the-wire ordering.

# SEE ALSO

**esunrpc_rpc_create**(3),
**esunrpc_rpc_call_async**(3),
**esunrpc_rpc_call_null**(3),
**esunrpc**(7).
