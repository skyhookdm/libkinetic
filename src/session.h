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
#ifndef _SESSION_H
#define _SESSION_H


/*
 * Session details user does not need to see
 */

#include "protocol_types.h"

typedef struct ksession {
	kbid_t           ks_bid;	// Next Session Batch ID
	uint32_t         ks_bats;	// Active Batches
	klimits_t        ks_l;		// Preserved session limits
	kconfiguration_t ks_conf;
	kcmdhdr_t        ks_ch;		// Preserved cmdhdr limits
	kstats_t	 ks_stats;	// Session stats
} ksession_t;

#endif // _SESSION_H
