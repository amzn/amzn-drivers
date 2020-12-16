// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENAPROPERTIES_HPP
#define _ENAPROPERTIES_HPP

#include "ENA.hpp"
#include "ENARings.hpp"

class ENAProperties
{
public:
	ENAProperties();
	~ENAProperties();

	bool isValid() const;

	const OSSymbol * getHWStatsSymbol(size_t i) const;
	const OSSymbol * getTxStatsSymbol(size_t i) const;
	const OSSymbol * getRxStatsSymbol(size_t i) const;

private:
	bool _valid;
	const OSSymbol *_hwStatsSymbols[ENAHWStatsNum];
	const OSSymbol *_txStatsSymbols[ENATxStatsNum];
	const OSSymbol *_rxStatsSymbols[ENARxStatsNum];

	static const char *_HWStatsNames[ENAHWStatsNum];
	static const char *_TxStatsNames[ENATxStatsNum];
	static const char *_RxStatsNames[ENARxStatsNum];
};

/***********************************************************
 * Interface functions
 ***********************************************************/

void copyStatsFromQueueToProperties(const AtomicStat *stats,
				    size_t count,
				    OSArray **queuesProperties,
				    unsigned int queueID);

/***********************************************************
 * Inline definitions
 ***********************************************************/

inline bool ENAProperties::isValid() const
{
	return _valid;
}

inline const OSSymbol * ENAProperties::getTxStatsSymbol(size_t i) const
{
	return _txStatsSymbols[i];
}

inline const OSSymbol * ENAProperties::getRxStatsSymbol(size_t i) const
{
	return _rxStatsSymbols[i];
}

inline const OSSymbol * ENAProperties::getHWStatsSymbol(size_t i) const
{
	return _hwStatsSymbols[i];
}

#endif
