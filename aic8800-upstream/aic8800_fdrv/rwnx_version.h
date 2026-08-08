/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RWNX_VERSION_H_
#define _RWNX_VERSION_H_

#include "rwnx_version_gen.h"
#include "aicwf_debug.h"

static inline void rwnx_print_version(void)
{
	// AICWFDBG(LOGINFO, RWNX_VERS_BANNER "\n");
	pr_info("AIC_WF RELEASE VERSION:%s \r\n", RELEASE_VERSION);
}

#endif /* _RWNX_VERSION_H_ */
