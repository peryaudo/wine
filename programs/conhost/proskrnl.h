/*
 * proskrnl console-server transport
 *
 * Copyright 2026 the proskrnl project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_CONHOST_PROSKRNL_H
#define __WINE_CONHOST_PROSKRNL_H

#include <stddef.h>
#include <windef.h>
#include <winternl.h>

/* On proskrnl there is no wineserver: conhost's server handle is an open of
 * the kernel's \Device\ConDrv\Server, and get_next_console_request becomes a
 * read/write wire protocol on it whose semantics mirror wineserver's
 * (server/console.c): a fetched blocking-read verb parks until a read=1
 * completion; any other fetched verb is completed by the next plain reply;
 * STATUS_PENDING from a fetch means "nothing queued, wait on the handle".
 *
 * The kernel side of this wire format lives in proskrnl's
 * drivers/condrvproto.h — the two definitions must stay byte-identical
 * (both are proskrnl-authored; neither is an NT contract).
 */

#define PROSKRNL_SERVER_BUFFER_SIZE 65536u

struct proskrnl_server_msg
{
    UINT64 id;         /* request id, echoed in the plain reply */
    UINT code;         /* IOCTL_CONDRV_* */
    UINT output;       /* screen-buffer id; 0 = the input object */
    UINT in_size;      /* payload bytes following this header */
    UINT out_capacity; /* most payload bytes the reply may carry */
};

struct proskrnl_server_reply
{
    UINT64 id;     /* the busy request this answers (read == 0) */
    int status;    /* NTSTATUS for the client's ioctl */
    UINT read;     /* 1 = complete the oldest delivered blocking read */
    UINT out_size; /* payload bytes following this header */
    UINT pad;
};

#define PROSKRNL_SERVER_MAX_PAYLOAD                                                                \
    (PROSKRNL_SERVER_BUFFER_SIZE - sizeof(struct proskrnl_server_msg))

/* TRUE when no wineserver is below (proskrnl). Probed once through the
 * seam ntdll's own fallbacks define: with no unixlib ever loaded,
 * wine_server_call answers STATUS_NOT_SUPPORTED (dlls/ntdll/loader.c) —
 * which a live wineserver never answers for the side-effect-free probe
 * request used. (__wine_unix_call_dispatcher itself is a -private ntdll
 * export a program cannot import.) */
extern BOOL proskrnl_transport_active(void);

/* Reply the previous request's result (unless none is outstanding) and fetch
 * the next one; STATUS_PENDING = nothing queued, wait on the server handle. */
extern NTSTATUS proskrnl_next_console_request(HANDLE server, NTSTATUS status, int signal,
                                              const void *reply_data, size_t reply_size,
                                              void *buffer, size_t buffer_capacity,
                                              unsigned int *code, int *output, size_t *out_size,
                                              size_t *in_size);

/* Complete the oldest delivered blocking read (read_complete's read=1 form);
 * data1 is the optional READ_CONSOLE_CONTROL key-state prefix. */
extern NTSTATUS proskrnl_console_read_complete(HANDLE server, NTSTATUS status, const void *data1,
                                               size_t size1, const void *data2, size_t size2,
                                               int signal);

#endif /* __WINE_CONHOST_PROSKRNL_H */
