// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENAUTILS_HPP
#define _ENAUTILS_HPP

#include <libkern/OSTypes.h>
#include <libkern/c++/OSArray.h>

#include <stdatomic.h>

#define SAFE_RELEASE(o) do {		\
	if (o != nullptr) {		\
		(o)->release();		\
		(o) = nullptr;		\
	}				\
} while(0)

#define ENA AmazonENAEthernet

typedef atomic_uint_fast64_t AtomicStat;

static inline void initAtomicStats(AtomicStat *stats, size_t count)
{
	for (size_t i = 0; i < count; ++i, ++stats)
		atomic_init(stats, 0);
}

static inline void addAtomicStat(AtomicStat *stat, UInt64 value)
{
	atomic_fetch_add_explicit(stat, value, memory_order_relaxed);
}

static inline void incAtomicStat(AtomicStat *stat)
{
	atomic_fetch_add_explicit(stat, 1, memory_order_relaxed);
}

static inline UInt64 getAtomicStat(const AtomicStat *stat)
{
	return atomic_load_explicit(stat, memory_order_relaxed);
}

static inline void setAtomicStat(AtomicStat *stat, UInt64 value)
{
	return atomic_store_explicit(stat, value, memory_order_relaxed);
}

OSArray *getArrayWithNumbers(unsigned int count);

#endif // _ENAUTILS_HPP
