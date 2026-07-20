/*
 * proskrnl console-server transport (see proskrnl.h)
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <ntstatus.h>

#include "proskrnl.h"
#include "wine/server.h"

/* Dead code under regular Wine: conhost.c calls in here only when
 * proskrnl_transport_active() latched TRUE, which never happens with a
 * unixlib (and wineserver) present. */

BOOL proskrnl_transport_active(void)
{
    static int active = -1;
    if (active < 0)
    {
        NTSTATUS status;
        /* A side-effect-free request: under a live wineserver this answers
         * STATUS_INVALID_HANDLE; with no unixlib below, ntdll's PE fallback
         * answers STATUS_NOT_SUPPORTED before any transport is touched. */
        SERVER_START_REQ( get_object_type )
        {
            req->handle = 0;
            status = wine_server_call( req );
        }
        SERVER_END_REQ;
        active = (status == STATUS_NOT_SUPPORTED);
    }
    return active > 0;
}

static char wire_buffer[PROSKRNL_SERVER_BUFFER_SIZE];
static BOOL have_stashed_msg;  /* fetched but not yet handed to conhost
                                * (its ioctl buffer needed growing) */
static UINT64 busy_request_id; /* != 0: the last delivered request wants a
                                * plain reply on the next call */

static NTSTATUS server_write( HANDLE server, const void *data, size_t size )
{
    IO_STATUS_BLOCK io;
    return NtWriteFile( server, NULL, NULL, NULL, &io, (void *)data, size, NULL, NULL );
}

NTSTATUS proskrnl_next_console_request( HANDLE server, NTSTATUS status, int signal,
                                        const void *reply_data, size_t reply_size,
                                        void *buffer, size_t buffer_capacity,
                                        unsigned int *code, int *output,
                                        size_t *out_size, size_t *in_size )
{
    struct proskrnl_server_msg *msg = (struct proskrnl_server_msg *)wire_buffer;
    IO_STATUS_BLOCK io;
    NTSTATUS ret;

    (void)signal; /* input-object signaling: not modeled by the kernel */

    if (!have_stashed_msg)
    {
        if (busy_request_id)
        {
            struct
            {
                struct proskrnl_server_reply hdr;
                char data[PROSKRNL_SERVER_MAX_PAYLOAD];
            } reply;
            if (reply_size > sizeof(reply.data)) reply_size = sizeof(reply.data);
            reply.hdr.id = busy_request_id;
            reply.hdr.status = status;
            reply.hdr.read = 0;
            reply.hdr.out_size = reply_size;
            reply.hdr.pad = 0;
            if (reply_size) memcpy( reply.data, reply_data, reply_size );
            busy_request_id = 0;
            ret = server_write( server, &reply, sizeof(reply.hdr) + reply_size );
            if (ret) return ret;
        }

        ret = NtReadFile( server, NULL, NULL, NULL, &io, wire_buffer, sizeof(wire_buffer),
                          NULL, NULL );
        if (ret) return ret; /* STATUS_PENDING = wait on the handle */
        have_stashed_msg = TRUE;
    }

    if (msg->in_size > buffer_capacity)
    {
        /* conhost grows its ioctl buffer and calls again; the fetched
         * message stays stashed (wineserver's BUFFER_OVERFLOW dance). */
        *out_size = msg->in_size;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (msg->in_size) memcpy( buffer, wire_buffer + sizeof(*msg), msg->in_size );
    *code = msg->code;
    *output = (int)msg->output;
    *out_size = msg->out_capacity;
    *in_size = msg->in_size;
    busy_request_id = msg->id; /* the kernel ignores it for blocking reads,
                                * exactly as wineserver does */
    have_stashed_msg = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS proskrnl_console_read_complete( HANDLE server, NTSTATUS status,
                                         const void *data1, size_t size1,
                                         const void *data2, size_t size2, int signal )
{
    struct
    {
        struct proskrnl_server_reply hdr;
        char data[PROSKRNL_SERVER_MAX_PAYLOAD];
    } reply;

    (void)signal;
    if (size1 + size2 > sizeof(reply.data)) return STATUS_INVALID_PARAMETER;
    reply.hdr.id = 0;
    reply.hdr.status = status;
    reply.hdr.read = 1;
    reply.hdr.out_size = size1 + size2;
    reply.hdr.pad = 0;
    if (size1) memcpy( reply.data, data1, size1 );
    if (size2) memcpy( reply.data + size1, data2, size2 );
    return server_write( server, &reply, sizeof(reply.hdr) + size1 + size2 );
}
