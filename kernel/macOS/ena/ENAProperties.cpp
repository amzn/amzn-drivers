// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENAProperties.hpp"

#include "ENALogs.hpp"

#define EP_ERROR_LOG(format, ...) ERROR_LOG(ENAProperties, format, ##__VA_ARGS__)

// HW stats
const char * ENAProperties::_HWStatsNames[ENAHWStatsNum] = {
	"ENARxPacketsTotal",
	"ENATxPacketsTotal",
	"ENARxBytesTotal",
	"ENATxBytesTotal",
	"ENARxDropsTotal",
	"ENAAdminQueuePause",
	"ENAWatchdogExpired",
};

// Tx stats
const char * ENAProperties::_TxStatsNames[ENATxStatsNum] = {
	"ENATxCount",
	"ENATxBytes",
	"ENATxPrepareContextErrors",
	"ENATxDMAMappingErrors",
	"ENATxDoorbells",
	"ENATxMissingCompletions",
	"ENATxBadRequestedID",
	"ENATxQueueWakeups",
	"ENATxQueueStops",
};

// Rx stats
const char * ENAProperties::_RxStatsNames[ENARxStatsNum] = {
	"ENARxCount",
	"ENARxBytes",
	"ENARxPartialRefills",
	"ENARxBadChecksum",
	"ENARxMbufAllocationFails",
	"ENARxDMAMappingErrors",
	"ENARxBadRequestedID",
	"ENARxBadDescriptorNumber",
};

ENAProperties::ENAProperties() : _valid(false)
{
	for (size_t i = 0; i < ENAHWStatsNum; ++i) {
		_hwStatsSymbols[i] = OSSymbol::withCString(_HWStatsNames[i]);
		if (_hwStatsSymbols[i] == nullptr) {
			EP_ERROR_LOG("Failed to allocate HW stats symbols\n");
			return;
		}
	}

	for (size_t i = 0; i < ENATxStatsNum; ++i) {
		_txStatsSymbols[i] = OSSymbol::withCString(_TxStatsNames[i]);
		if (_txStatsSymbols[i] == nullptr) {
			EP_ERROR_LOG("Failed to allocate Tx stats symbols\n");
			return;
		}
	}

	for (size_t i = 0; i < ENARxStatsNum; ++i) {
		_rxStatsSymbols[i] = OSSymbol::withCString(_RxStatsNames[i]);
		if (_rxStatsSymbols[i] == nullptr) {
			EP_ERROR_LOG("Failed to allocate Rx stats symbols\n");
			return;
		}
	}

	_valid = true;
}

ENAProperties::~ENAProperties()
{
	for (auto &symbol : _hwStatsSymbols)
		SAFE_RELEASE(symbol);

	for (auto &symbol : _txStatsSymbols)
		SAFE_RELEASE(symbol);

	for (auto &symbol : _rxStatsSymbols)
		SAFE_RELEASE(symbol);
}

void copyStatsFromQueueToProperties(const AtomicStat *stats,
				    size_t count,
				    OSArray **queuesProperties,
				    unsigned int queueID)
{
	OSNumber *propertyNumber;

	for (size_t i = 0; i < count; ++i) {
		propertyNumber = OSDynamicCast(OSNumber, queuesProperties[i]->getObject(queueID));
		propertyNumber->setValue(getAtomicStat(&stats[i]));
	}
}
