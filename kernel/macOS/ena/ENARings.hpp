// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENARINGS_HPP
#define _ENARINGS_HPP

#include "ENATask.hpp"
#include "ENATime.hpp"
#include "ENAUtils.hpp"

#include "ena_com/ena_eth_com.h"

#include <sys/kernel_types.h>

#include <IOKit/network/IOMbufMemoryCursor.h>

constexpr UInt16 enaPktMaxBufs = 19;

/***********************************************************
 * ENARing class
 ***********************************************************/

class ENA;

class ENARing : public OSObject
{
	OSDeclareAbstractStructors(ENARing)
public:
	struct Config
	{
		enum ena_admin_placement_policy_type memQueueType;
		UInt32 queueSize;
		UInt32 maxSegmentSize;
		UInt16 maxSegmentsNumber;
		UInt16 packetCleanupBudget;
		UInt16 qid;
	};

	// The enable and the disable should only be called after the ENA
	// controller was successfully started.
	virtual bool enable();
	virtual void disable();

	UInt16 getPacketCleanupBudget();

	virtual int interruptProcess() = 0;

protected:
	IOMbufNaturalMemoryCursor *_mbufCursor;
	ENA			*_controller;
	struct ena_com_io_sq	*_ioSQ;
	struct ena_com_io_cq	*_ioCQ;
	enum ena_admin_placement_policy_type _memQueueType;
	enum queue_direction    _direction;
	UInt32			_queueSize;
	UInt16			*_freeIDs;
	UInt16			_packetCleanupBudget;
	UInt16			_maxSegNum;	// Maximum number of segments
	UInt16			_nextToUse;
	UInt16			_nextToClean;
	UInt16			_qid;
	UInt16			_ioqid;
	bool			_firstInterrupt;

	bool init(ENA *controller, Config *config);
	virtual void free() APPLE_KEXT_OVERRIDE;

	bool createIOQueue();
	void destroyIOQueue();

	virtual void freeBuffers() = 0;

	void advanceIndex(UInt16 &index);

	virtual bool validateReqID(UInt16 reqID);
};

/***********************************************************
 * ENARingTx class and dependencies
 ***********************************************************/

struct ENATxBuffer
{
	mbuf_t			mbuf;
	struct ena_com_buf	enaBufs[enaPktMaxBufs];
	int			descs;
	ENATime			timestamp;
	bool			printOnce;
};

struct ENATxStats
{
	AtomicStat count;
	AtomicStat bytes;
	AtomicStat prepareContextErrors;
	AtomicStat dmaMappingErrors;
	AtomicStat doorbells;
	AtomicStat missingCompletions;
	AtomicStat badRequestedID;
	AtomicStat queueWakeups;
	AtomicStat queueStops;
};
constexpr size_t ENATxStatsNum = sizeof(ENATxStats)/sizeof(AtomicStat);

class ENARingTx : public ENARing
{
	OSDeclareDefaultStructors(ENARingTx)
public:
	typedef ENARing::Config TxConfig;

	static ENARingTx * withConfig(ENA *controller, TxConfig *config);

	bool enable() APPLE_KEXT_OVERRIDE;
	void disable() APPLE_KEXT_OVERRIDE;

	int interruptProcess() APPLE_KEXT_OVERRIDE;

	UInt32 sendPacket(mbuf_t mbuf, void *param);

	bool checkForMissingCompletions();
	void setupMissingTxCompletion(ENATime timeout, UInt32 threshold);

	const ENATxStats * getTxStats() const;

private:
	ENATxBuffer		*_bufferInfo;
	ENATxStats		_txStats;
	ENATime			_missingTxCompletionTimeout;
	UInt32			_missingTxCompletionThreshold;
	UInt16			_doorbellBurstCount;
	bool			_txStalled;

	constexpr static UInt16 DOORBELL_BURST_THRESHOLD = 32;

	bool init(ENA *controller, TxConfig *config);
	void freeBuffers() APPLE_KEXT_OVERRIDE;

	bool validateReqID(UInt16 reqID) APPLE_KEXT_OVERRIDE;
};

/***********************************************************
 * ENARingRx class and dependencies
 ***********************************************************/

struct ENARxBuffer
{
	mbuf_t			mbuf;
	struct ena_com_buf	enaBuf;
};

struct ENARxStats
{
	AtomicStat count;
	AtomicStat bytes;
	AtomicStat partialRefills;
	AtomicStat badChecksum;
	AtomicStat mbufAllocationFails;
	AtomicStat dmaMappingErrors;
	AtomicStat badRequestedID;
	AtomicStat badDescriptorNumber;
};
constexpr size_t ENARxStatsNum = sizeof(ENARxStats)/sizeof(AtomicStat);

class ENARingRx : public ENARing
{
	OSDeclareDefaultStructors(ENARingRx)
public:
	typedef ENARing::Config RxConfig;

	static ENARingRx * withConfig(ENA *controller, RxConfig *config);

	bool enable() APPLE_KEXT_OVERRIDE;
	void disable() APPLE_KEXT_OVERRIDE;

	int interruptProcess() APPLE_KEXT_OVERRIDE;

	bool checkForInterrupt();

	const ENARxStats * getRxStats() const;

private:
	struct ena_com_rx_buf_info _enaBufs[enaPktMaxBufs];
	ENARxBuffer		*_bufferInfo;
	ENARxStats		_rxStats;
	UInt32			_noInterruptEventCount;

	bool init(ENA *controller, RxConfig *config);

	bool allocMbuf(ENARxBuffer* rxInfo);

	UInt32 refillBuffers(UInt32 number);
	void freeBuffers() APPLE_KEXT_OVERRIDE;

	bool validateReqID(UInt16 reqID) APPLE_KEXT_OVERRIDE;

	mbuf_t rxMbuf(struct ena_com_rx_ctx *enaRxContext);
};

/***********************************************************
 * ENAQueue class
 ***********************************************************/

class ENAQueue : public OSObject
{
	OSDeclareDefaultStructors(ENAQueue)
public:
	static ENAQueue * withConfig(ENA *controller,
				     ENARingTx::TxConfig *txConfig,
				     ENARingRx::RxConfig *rxConfig);

	bool enable();
	void disable();

	ENARingTx * getTxRing();
	ENARingRx * getRxRing();

	UInt16 getID();

	void unmaskIOInterrupt(UInt32 rxDelay = 0, UInt32 txDelay = 0);
	void interruptOccurred();

private:
	ENA			*_controller;
	ENARingTx 		*_txRing;
	ENARingRx 		*_rxRing;
	ENATask			*_ioTask;
	UInt16			_qid;

	bool init(ENA *controller,
		  ENARingTx::TxConfig *txConfig,
		  ENARingRx::RxConfig *rxConfig);
	void free() APPLE_KEXT_OVERRIDE;

	bool enableIOInterruptHandler();
	void disableIOInterruptHandler();

	void handleInterrupt();
};

#endif // _ENARINGS_HPP
