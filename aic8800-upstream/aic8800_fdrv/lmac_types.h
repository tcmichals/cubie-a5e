/* SPDX-License-Identifier: GPL-2.0 */
/*
 ****************************************************************************************
 *
 * Copyright (C) 2020 AIC semiconductor.
 *
 * @file co_types.h
 *
 * @brief This file replaces the need to include stdint or stdbool typical
 *headers, which may not be available in all toolchains, and adds new types
 *
 ****************************************************************************************
 */

#ifndef _LMAC_INT_H_
#define _LMAC_INT_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/bits.h>

#ifdef CONFIG_RWNX_TL4
typedef u16 u8_l;
typedef s16 s8_l;
typedef u16 bool_l;
#else
typedef u8 u8_l;
typedef s8 s8_l;
#endif
typedef u16 u16_l;
typedef s16 s16_l;
typedef u32 u32_l;
typedef s32 s32_l;
typedef u64 u64_l;

/// @} CO_INT
#endif // _LMAC_INT_H_
