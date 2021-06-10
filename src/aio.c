/**
 * Copyright 2020-2021 Seagate Technology LLC.
 *
 * This Source Code Form is subject to the terms of the Mozilla
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 * https://mozilla.org/MP:/2.0/.
 *
 * This program is distributed in the hope that it will be useful,
 * but is provided AS-IS, WITHOUT ANY WARRANTY; including without
 * the implied warranty of MERCHANTABILITY, NON-INFRINGEMENT or
 * FITNESS FOR A PARTICULAR PURPOSE. See the Mozilla Public
 * License for more details.
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <endian.h>
#include <errno.h>

#include "kio.h"
#include "ktli.h"
#include "kinetic.h"
#include "kinetic_internal.h"
#include "protocol_interface.h"

kstatus_t g_get_aio_complete(int ktd,   struct kio *kio, void **cctx);
kstatus_t p_put_aio_complete(int ktd,   struct kio *kio, void **cctx);
kstatus_t d_del_aio_complete(int ktd,   struct kio *kio, void **cctx);
kstatus_t b_batch_aio_complete(int ktd, struct kio *kio, void **cctx);
kstatus_t n_noop_aio_complete(int ktd,  struct kio *kio, void **cctx);
kstatus_t f_flush_aio_complete(int ktd, struct kio *kio, void **cctx);
kstatus_t e_exec_aio_complete(int ktd,  struct kio *kio, void **cctx);

kstatus_t
ki_aio_complete(int ktd, kio_t *ckio, void **cctx)
{
	struct kio *kio = (struct kio *)ckio;
	kstatus_t ks;
	
	switch (kio->kio_cmd) {
	case KMT_PUT:
		ks = p_put_aio_complete(ktd, kio, cctx);
		break;
	case KMT_DEL:
		ks = d_del_aio_complete(ktd, kio, cctx);
		break;
	case KMT_GET:
	case KMT_GETNEXT:
	case KMT_GETPREV:
	case KMT_GETVERS:
		ks = g_get_aio_complete(ktd, kio, cctx);
		break;
		
	case KMT_STARTBAT:
	case KMT_ABORTBAT:
	case KMT_ENDBAT:
		ks = b_batch_aio_complete(ktd, kio, cctx);
		break;

	case KMT_NOOP:
		ks = n_noop_aio_complete(ktd, kio, cctx);
		break;

	case KMT_FLUSH:
		ks = f_flush_aio_complete(ktd, kio, cctx);
		break;

	case KMT_PUSHP2P:
		break;

	case KMT_APPLET:
		ks = e_exec_aio_complete(ktd, kio, cctx);
		break;

	default:
		debug_printf("aio_complete: bad cmd");
		return(K_EINVAL);
	}

	return(ks);
}

int
ki_poll(int ktd, int timeout)
{
	return(ktli_poll(ktd, timeout));
}
