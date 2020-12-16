// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENARings.hpp"

#include "ENA.hpp"
#include "ENALogs.hpp"

#include <sys/param.h>

/***********************************************************
 * ENARing definition
 ***********************************************************/

OSDefineMetaClassAndAbstractStructors(ENARing, OSObject)

bool ENARing::init(ENA *controller, ENARing::Config *config)
{
	if (!OSObject::init())
		return false;

	_mbufCursor = IOMbufNaturalMemoryCursor::withSpecification(
		config->maxSegmentSize,
		config->maxSegmentsNumber);
	if (_mbufCursor == nullptr) {
		ENA_ERROR_LOG_EXT(_controller, "Failed to create mbuf memory cursor\n");
		return false;
	}
	_qid = config->qid;

	_controller = controller;

	_memQueueType = config->memQueueType;
	_queueSize = config->queueSize;
	_maxSegNum = config->maxSegmentsNumber;
	_packetCleanupBudget = config->packetCleanupBudget;
	_firstInterrupt = false;

	return true;
}

void ENARing::free()
{
	SAFE_RELEASE(_mbufCursor);

	OSObject::free();
}

bool ENARing::enable()
{
	_freeIDs = IONew(UInt16, _queueSize);
	if (_freeIDs == nullptr) {
		ENA_DEBUG_LOG_EXT(_controller, "Cannot allocate freeIDs\n");
		return false;
	}

	for (UInt16 i = 0; i < _queueSize; ++i)
		_freeIDs[i] = i;

	// Reset the ring indexes
	_nextToClean = 0;
	_nextToUse = 0;

	if (!createIOQueue()) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to create ena_com IO queues\n");
		return false;
	}

	return true;
}

void ENARing::disable()
{
	if (_freeIDs != nullptr) {
		IODelete(_freeIDs, UInt16, _queueSize);
		_freeIDs = nullptr;
	}

	destroyIOQueue();

	freeBuffers();
}

bool ENARing::createIOQueue()
{
	struct ena_com_dev *enaDev = _controller->getENADevice();
	struct ena_com_create_io_ctx ctx;
	int rc;

	ctx.qid = _ioqid;
	ctx.direction = _direction;
	ctx.queue_size = _queueSize;
	ctx.mem_queue_type = _memQueueType;
	// Every IO interrupt must be handled on vector 0 because of Darwin
	// limitations.
	ctx.msix_vector = 0;

	ENA_DEBUG_LOG_EXT(_controller,
		      "Creating IO queues for ring %d: qid %d, dir %d, size %d, type %d\n",
		      _qid, _ioqid, _direction, _queueSize, _memQueueType);

	rc = ena_com_create_io_queue(enaDev, &ctx);
	if (rc != 0) {
		ENA_ERROR_LOG_EXT(_controller,
			      "Failed to create ena_com IO queue #%d rc: %d\n",
			      _qid, rc);
		return false;
	}

	rc = ena_com_get_io_handlers(enaDev, _ioqid, &_ioSQ, &_ioCQ);
	if (rc != 0) {
		ENA_ERROR_LOG_EXT(_controller,
			      "Failed to get ena_com IO queue's handlers #%d rc: %d\n",
			      _qid, rc);
		return false;
	}

	return true;
}

void ENARing::destroyIOQueue()
{
	if (_ioCQ != nullptr && _ioSQ != nullptr) {
		ena_com_destroy_io_queue(_controller->getENADevice(), _ioqid);
		_ioCQ = nullptr;
		_ioSQ = nullptr;
	}
}

void ENARing::advanceIndex(UInt16 &index)
{
	index = (index + 1) & (_queueSize - 1);
}

bool ENARing::validateReqID(UInt16 reqID)
{
	if (unlikely(reqID >= _queueSize)) {
		ENA_ERROR_LOG_EXT(_controller, "Invalid requested ID: %hu\n", reqID);
		return false;
	}

	return true;
}

UInt16 ENARing::getPacketCleanupBudget()
{
	return _packetCleanupBudget;
}

/***********************************************************
 * ENARingTx definition
 ***********************************************************/

OSDefineMetaClassAndStructors(ENARingTx, ENARing)

ENARingTx * ENARingTx::withConfig(ENA *controller, ENARingTx::TxConfig *config)
{
	ENARingTx *ring = new ENARingTx;

	if (ring != nullptr) {
		if (!ring->init(controller, config)) {
			ring->release();
			ring = nullptr;
		}
	}

	return ring;
}

bool ENARingTx::init(ENA *controller, ENARingTx::TxConfig *config)
{
	if (!ENARing::init(controller, config))
		return false;
	_direction = ENA_COM_IO_QUEUE_DIRECTION_TX;
	_ioqid = ENA_IO_TXQ_IDX(_qid);
	_missingTxCompletionTimeout = ENA_HW_HINTS_NO_TIMEOUT;
	// By default set the threshold to max, it can be overwritten by the
	// setupMissingTxCompletion function;
	_missingTxCompletionThreshold = 0xffffffff;
	initAtomicStats((AtomicStat *)&_txStats, ENATxStatsNum);

	return true;
}

bool ENARingTx::enable()
{
	ENA_DEBUG_LOG_EXT(_controller, "Enabling Tx ring %d\n", _qid);

	if (!ENARing::enable()) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to enable ENARing\n");
		return false;
	}

	_bufferInfo = IONew(ENATxBuffer, _queueSize);
	if (_bufferInfo == nullptr) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to allocate buffer info\n");
		return false;
	}
	memset(_bufferInfo, 0 , _queueSize * sizeof(ENATxBuffer));

	_doorbellBurstCount = 0;

	return true;
}

void ENARingTx::disable()
{
	if (_bufferInfo != nullptr) {
		IODelete(_bufferInfo, ENATxBuffer, _queueSize);
		_bufferInfo = nullptr;
	}

	ENARing::disable();
}

void ENARingTx::freeBuffers()
{
	bool printOnce = true;

	if (_bufferInfo == nullptr)
		return;

	for (int i = 0; i < _queueSize; i++) {
		ENATxBuffer *txInfo = &_bufferInfo[i];

		if (txInfo->mbuf == nullptr)
			continue;

		if (printOnce) {
			ENA_ERROR_LOG_EXT(_controller,
				      "Free uncompleted Tx mbuf on qid %d, idx 0x%x\n",
				      _qid, i);
			printOnce = false;
		} else {
			ENA_DEBUG_LOG_EXT(_controller,
				      "Free uncompleted Tx mbuf on qid %d, idx 0x%x\n",
				      _qid, i);
		}

		_controller->freePacket(txInfo->mbuf, 0);
		txInfo->mbuf = nullptr;
	}
}

int ENARingTx::interruptProcess()
{
	ENAParallelOutputQueue *outputQueue;
	ENATxBuffer *txInfo;
	size_t commit = ENA_TX_COMMIT;
	int rc;
	UInt16 packetsDone = 0;
	UInt16 descsDone = 0;
	UInt16 reqID;
	UInt16 ntc;

	_firstInterrupt = true;

	ntc = _nextToClean;
	do {
		rc = ena_com_tx_comp_req_id_get(_ioCQ, &reqID);
		if (rc != 0)
			break;

		if (!validateReqID(reqID))
			break;

		txInfo = &_bufferInfo[reqID];
		_controller->freePacket(txInfo->mbuf);
		txInfo->mbuf = nullptr;
		txInfo->timestamp = 0;

		descsDone += txInfo->descs;
		++packetsDone;

		_freeIDs[ntc] = reqID;
		advanceIndex(ntc);

		// Update ring state every ENA_TX_COMMIT descriptor.
		commit--;
		if (unlikely(commit == 0)) {
			commit = ENA_TX_COMMIT;
			_nextToClean = ntc;
			ena_com_comp_ack(_ioSQ, descsDone);
			ena_com_update_dev_comp_head(_ioCQ);
			descsDone = 0;
		}
	} while (likely(packetsDone < _packetCleanupBudget));

	// If there is still something to commit update ring state.
	if (likely(commit != ENA_TX_COMMIT)) {
		_nextToClean = ntc;
		ena_com_comp_ack(_ioSQ, descsDone);
		ena_com_update_dev_comp_head(_ioCQ);
	}

	// Make the ring update visible to the Tx routine
	atomic_thread_fence(memory_order_acq_rel);
	if (unlikely(_txStalled && ena_com_sq_have_enough_space(_ioSQ, _maxSegNum + 2))) {
		_txStalled = false;
		outputQueue = OSDynamicCast(ENAParallelOutputQueue,
					    _controller->getOutputQueue());
		outputQueue->service(_qid, IOBasicOutputQueue::kServiceAsync);
		incAtomicStat(&_txStats.queueWakeups);
	}

	return packetsDone;
}

bool ENARingTx::checkForMissingCompletions()
{
	ENATxBuffer *txBuffer;
	ENATime lastTime;
	UInt32 missedTx = 0;

	for (int i = 0; i < _queueSize; ++i) {
		txBuffer = &_bufferInfo[i];
		lastTime = txBuffer->timestamp;

		if (lastTime == ENATime(0))
			// No pending Tx at this location
			continue;

		if (unlikely(!_firstInterrupt &&
			     ENATime::getNow() > (lastTime + 2 * _missingTxCompletionTimeout))) {
			// If after graceful period interrupt is still not
			// received, we schedule a reset
			ENA_ERROR_LOG_EXT(_controller,
				      "Potential MSIX issue on Tx side Queue = %d. Reset the device\n",
				      _qid);
			_controller->triggerReset(ENA_REGS_RESET_MISS_INTERRUPT);
			return false;
		}

		if (unlikely(ENATime::getNow() > (lastTime + _missingTxCompletionTimeout))) {
			if (!txBuffer->printOnce)
				ENA_ERROR_LOG_EXT(_controller,
					     "Found a Tx that wasn't completed on time, qid %d, index %d.\n",
					     _qid, i);

			txBuffer->printOnce = true;
			missedTx++;
		}
	}

	if (unlikely(missedTx > _missingTxCompletionThreshold)) {
		ENA_ERROR_LOG_EXT(_controller,
			      "The number of lost tx completions is above the threshold (%d > %d). Reset the device\n",
			      missedTx, _missingTxCompletionThreshold);
		_controller->triggerReset(ENA_REGS_RESET_MISS_TX_CMPL);
		return false;
	}

	addAtomicStat(&_txStats.missingCompletions, missedTx);

	return true;
}

void ENARingTx::setupMissingTxCompletion(ENATime timeout, UInt32 threshold)
{
	_missingTxCompletionTimeout = timeout;
	_missingTxCompletionThreshold = threshold;
}

UInt32 ENARingTx::sendPacket(mbuf_t mbuf, void *param)
{
	IOPhysicalSegment segments[enaPktMaxBufs];
	IONetworkStats *netStats;
	ENAParallelOutputQueue *outputQueue;
	ENATxBuffer *txInfo;
	ENAHWStats *hwStats;
	struct ena_com_tx_ctx enaTxCtx = {};
	int rc, nbHwDesc;
	UInt32 ret = kIOReturnOutputSuccess;
	UInt32 segNum;
	UInt16 reqID;

	netStats = _controller->getNetworkStats();
	hwStats = _controller->getHWStats();

	reqID = _freeIDs[_nextToUse];
	txInfo = &_bufferInfo[reqID];

	// Get segments but reserve 1 descriptor for meta-data
	segNum = _mbufCursor->getPhysicalSegmentsWithCoalesce(mbuf, segments, _maxSegNum - 1);
	if (segNum == 0) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to map mbuf to segments\n");
		++netStats->outputErrors;
		incAtomicStat(&_txStats.dmaMappingErrors);
		_controller->freePacket(mbuf);
		return kIOReturnOutputDropped;
	}

	for (int i = 0; i < segNum; i++) {
		txInfo->enaBufs[i].len = segments[i].length;
		txInfo->enaBufs[i].paddr = segments[i].location;
	}

	enaTxCtx.ena_bufs = txInfo->enaBufs;
	enaTxCtx.num_bufs = segNum;
	enaTxCtx.req_id = reqID;

	_controller->setupTxChecksum(enaTxCtx, mbuf);

	rc = ena_com_prepare_tx(_ioSQ, &enaTxCtx, &nbHwDesc);
	if (unlikely(rc != 0)) {
		ENA_ERROR_LOG_EXT(_controller, "Failed to prepare Tx bufs on queue %d\n", _qid);
		++netStats->outputErrors;
		incAtomicStat(&_txStats.prepareContextErrors);
		if (rc != ENA_COM_NO_MEM) {
			_controller->triggerReset(ENA_REGS_RESET_DRIVER_INVALID_STATE);
		}
		_controller->freePacket(mbuf);
		return kIOReturnOutputDropped;
	}

	ENA_DEBUG_LOG_EXT(_controller, "Tx mbuf on queue %d: %zu bytes, %u segs, %d hwDescs\n",
		      _qid, mbuf_pkthdr_len(mbuf), segNum, nbHwDesc);
	incAtomicStat(&_txStats.count);
	addAtomicStat(&_txStats.bytes, mbuf_pkthdr_len(mbuf));

	++_doorbellBurstCount;

	txInfo->descs = nbHwDesc;
	txInfo->mbuf = mbuf;
	txInfo->timestamp.setNow();

	advanceIndex(_nextToUse);

	// This barrier is aimed to perform barrier before reading next_to_completion
	atomic_thread_fence(memory_order_acquire);

	// Stop the queue when no more space available, the packet can have up
	// to _maxSegNum + 2. One for the meta descriptor and one for header
	// (if the header is larger than txMaxHeaderSize - which is not implemented, yet).
	if (unlikely(!ena_com_sq_have_enough_space(_ioSQ, _maxSegNum + 2))) {
		ENA_DEBUG_LOG_EXT(_controller, "Tx queue %d stopped\n", _qid);
		ret |= kIOOutputCommandStall;
		_txStalled = true;
		incAtomicStat(&_txStats.queueStops);
	}

	outputQueue = OSDynamicCast(ENAParallelOutputQueue, _controller->getOutputQueue());
	if (((ret & kIOOutputCommandMask) == kIOOutputCommandStall) ||
	    (outputQueue->getSize(_qid) == 0) ||
	    (_doorbellBurstCount == DOORBELL_BURST_THRESHOLD)) {
		ena_com_write_sq_doorbell(_ioSQ);
		incAtomicStat(&_txStats.doorbells);
		_doorbellBurstCount = 0;
	}

	return ret;
}

bool ENARingTx::validateReqID(UInt16 reqID)
{
	if (ENARing::validateReqID(reqID)) {
		ENATxBuffer *txInfo = &_bufferInfo[reqID];
		if (likely(txInfo->mbuf != nullptr))
			return true;
		ENA_ERROR_LOG_EXT(_controller, "TX info doesn't have valid mbuf\n");
	}

	incAtomicStat(&_txStats.badRequestedID);
	_controller->triggerReset(ENA_REGS_RESET_INV_TX_REQ_ID);

	return false;
}

const ENATxStats * ENARingTx::getTxStats() const
{
	return &_txStats;
}

/***********************************************************
 * ENARingRx definition
 ***********************************************************/

OSDefineMetaClassAndStructors(ENARingRx, ENARing)

ENARingRx * ENARingRx::withConfig(ENA *controller, ENARingRx::RxConfig *config)
{
	ENARingRx *ring = new ENARingRx;

	if (ring != nullptr) {
		if (!ring->init(controller, config)) {
			ring->release();
			ring = nullptr;
		}
	}

	return ring;
}

bool ENARingRx::init(ENA *controller, ENARingRx::RxConfig *config)
{
	if (!ENARing::init(controller, config))
		return false;


	_direction = ENA_COM_IO_QUEUE_DIRECTION_RX;
	_ioqid = ENA_IO_RXQ_IDX(_qid);
	initAtomicStats((AtomicStat *)&_rxStats, ENARxStatsNum);

	return true;
}

bool ENARingRx::enable()
{
	int toRefill, refilled;

	ENA_DEBUG_LOG_EXT(_controller, "Enabling Rx ring %d\n", _qid);

	if (!ENARing::enable()) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to enable ENARing\n");
		return false;
	}

	_bufferInfo = IONew(ENARxBuffer, _queueSize);
	if (_bufferInfo == nullptr) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to allocate buffer info\n");
		return false;
	}
	memset(_bufferInfo, 0 , _queueSize * sizeof(ENARxBuffer));

	toRefill = _queueSize - 1;
	refilled = refillBuffers(toRefill);
	if (toRefill != refilled) {
		ENA_ERROR_LOG_EXT(_controller,
			      "Refilling queue %d failed. Allocated %d buffers from %d\n",
			      _qid, refilled, toRefill);
	}

	return true;
}

void ENARingRx::disable()
{
	freeBuffers();

	if (_bufferInfo != nullptr) {
		IODelete(_bufferInfo, ENARxBuffer, _queueSize);
		_bufferInfo = nullptr;
	}

	ENARing::disable();
}

bool ENARingRx::allocMbuf(ENARxBuffer* rxInfo)
{
	IOMemoryCursor::PhysicalSegment seg;
	errno_t error;
	unsigned int maxChunks;
	int nsegs;
	int mlen;

	// If previously allocated buffer was not used
	if (rxInfo->mbuf != NULL)
		return true;

	maxChunks = 1;
	// There is no guarantee that bigger cluster will be physically contiguous
	mlen = MBIGCLBYTES;
	// Get mbuf
	error = mbuf_allocpacket(MBUF_WAITOK,
				 mlen,
				 &maxChunks,
				 &rxInfo->mbuf);
	if (error != 0) {
		ENA_DEBUG_LOG_EXT(_controller,
			      "Failed to allocate mbuf, error: %d\n", error);
		incAtomicStat(&_rxStats.mbufAllocationFails);
		return false;
	}

	mbuf_pkthdr_setlen(rxInfo->mbuf, mlen);
	mbuf_setlen(rxInfo->mbuf, mlen);

	// Map packets for DMA - only single segment is allowed. We are using
	// cursor to make sure that the cluster is physically contiguous
	nsegs = _mbufCursor->getPhysicalSegmentsWithCoalesce(rxInfo->mbuf, &seg, 1);
	if (unlikely(nsegs != 1)) {
		ENA_DEBUG_LOG_EXT(_controller, "Failed to map rx mbuf, nsegs %d\n", nsegs);
		incAtomicStat(&_rxStats.dmaMappingErrors);
		_controller->freePacket(rxInfo->mbuf);
		rxInfo->mbuf = nullptr;
		return false;
	}

	rxInfo->enaBuf.paddr = seg.location;
	rxInfo->enaBuf.len = seg.length;

	return true;
}

UInt32 ENARingRx::refillBuffers(UInt32 number)
{
	ENARxBuffer *rxInfo;
	UInt32 i;
	UInt16 reqID;
	int rc;

	ENA_DEBUG_LOG_EXT(_controller, "Refill of the Rx queue %d\n", _qid);
	for (i = 0; i < number; i++) {
		reqID = _freeIDs[_nextToUse];
		rxInfo = &_bufferInfo[reqID];

		if (!allocMbuf(rxInfo)) {
			ENA_DEBUG_LOG_EXT(_controller, "Failed to alloc buffer for Rx queue %d\n", _qid);
			break;
		}

		rc = ena_com_add_single_rx_desc(_ioSQ, &rxInfo->enaBuf, reqID);
		if (rc != 0) {
			ENA_DEBUG_LOG_EXT(_controller, "Failed to add descriptor for Rx queue %d\n", _qid);
			break;
		}

		advanceIndex(_nextToUse);
	}

	if (i < number) {
		incAtomicStat(&_rxStats.partialRefills);
		ENA_DEBUG_LOG_EXT(_controller,
			      "Refilled Rx queue %d with only %d mbufs (from %d)\n",
			      _qid, i, number);
	}

	if (i != 0)
		ena_com_write_sq_doorbell(_ioSQ);

	return i;
}

void ENARingRx::freeBuffers()
{
	if (_bufferInfo == nullptr)
		return;

	for (int i = 0; i < _queueSize; i++) {
		ENARxBuffer *rxInfo = &_bufferInfo[i];
		if (rxInfo->mbuf != nullptr) {
			_controller->freePacket(rxInfo->mbuf, 0);
			rxInfo->mbuf = nullptr;
		}
	}
}

bool ENARingRx::validateReqID(UInt16 reqID)
{
	if (unlikely(!ENARing::validateReqID(reqID))) {
		incAtomicStat(&_rxStats.badRequestedID);
		_controller->triggerReset(ENA_REGS_RESET_INV_RX_REQ_ID);
		return false;
	}

	return true;
}

int ENARingRx::interruptProcess()
{
	struct ena_com_rx_ctx enaRxCtx;
	IOEthernetInterface *netif;
	IONetworkStats *netStats;
	ENAHWStats *hwStats;
	mbuf_t mbuf;
	UInt64 totalBytesReceived = 0;
	UInt32 refillThreshold;
	UInt32 refillRequired;
	UInt16 totalPacketsReceived;
	int budget = _packetCleanupBudget;
	int rc;

	_firstInterrupt = true;

	netif = _controller->getEthernetInterface();
	netStats = _controller->getNetworkStats();
	hwStats = _controller->getHWStats();

	enaRxCtx.ena_bufs = _enaBufs;
	enaRxCtx.max_bufs = _maxSegNum;

	assertf(budget > 0, "Rx budget for queue %d was not set!", _qid);
	do {
		enaRxCtx.descs = 0;

		rc = ena_com_rx_pkt(_ioCQ, _ioSQ, &enaRxCtx);
		if (rc != 0) {
			ENA_ERROR_LOG_EXT(_controller,
				      "Failed to get the Rx packet from the device on queue %d, rc: %d\n",
				      _qid, rc);
			_controller->incrementRxErrors();
			incAtomicStat(&_rxStats.badDescriptorNumber);
			_controller->triggerReset(ENA_REGS_RESET_TOO_MANY_RX_DESCS);
			return 0;
		}

		if (enaRxCtx.descs == 0)
			break;

		ENA_DEBUG_LOG_EXT(
			_controller,
			"Rx: queue %d got packet from ENA. descs #: %d l3 proto %d l4 proto %d hash: %x\n",
			_qid, enaRxCtx.descs, enaRxCtx.l3_proto, enaRxCtx.l4_proto, enaRxCtx.hash);

		// Receive the data from the ring as an mbuf
		mbuf = rxMbuf(&enaRxCtx);
		// Exit if we failed to retrieve a buffer
		if(mbuf == nullptr) {
			for (int i = 0; i < enaRxCtx.descs; i++) {
				_freeIDs[_nextToClean] = _enaBufs[i].req_id;
				advanceIndex(_nextToClean);
			}
			_controller->incrementRxErrors();
			break;
		}
		totalBytesReceived += mbuf_pkthdr_len(mbuf);

		// Send the packet to the stack
		_controller->lockInputQueue();
		netif->inputPacket(mbuf, 0, IONetworkInterface::kInputOptionQueuePacket);
		_controller->unlockInputQueue();
	} while(--budget);

	_controller->lockInputQueue();
	netif->flushInputQueue();
	_controller->unlockInputQueue();

	totalPacketsReceived = _packetCleanupBudget - budget;

	addAtomicStat(&_rxStats.bytes, totalBytesReceived);
	addAtomicStat(&_rxStats.count, totalPacketsReceived);

	refillRequired = ena_com_free_q_entries(_ioSQ);
	refillThreshold = _queueSize / ENA_RX_REFILL_THRESH_DIVIDER;

	if (refillRequired > refillThreshold) {
		ena_com_update_dev_comp_head(_ioCQ);
		refillBuffers(refillRequired);
	}

	return totalPacketsReceived;
}

bool ENARingRx::checkForInterrupt()
{
	if (likely(_firstInterrupt))
		return true;

	if (ena_com_cq_empty(_ioCQ))
		return true;

	++_noInterruptEventCount;

	if (_noInterruptEventCount == ENA_MAX_NO_INTERRUPT_ITERATIONS) {
		ENA_ERROR_LOG_EXT(_controller,
			      "Potential MSIX issue on Rx side Queue = %d. Reset the device\n",
			      _qid);

		_noInterruptEventCount = 0;
		_controller->triggerReset(ENA_REGS_RESET_MISS_INTERRUPT);
		return false;
	}

	return true;
}

mbuf_t ENARingRx::rxMbuf(struct ena_com_rx_ctx *enaRxCtx)
{
	ENARxBuffer *rxInfo;
	mbuf_t mbuf;
	size_t totalSize = 0;
	UInt16 descs;
	UInt16 ntc, len, reqID;
	UInt16 buf;

	descs = enaRxCtx->descs;
	ntc = _nextToClean;
	rxInfo = &_bufferInfo[ntc];

	if (unlikely(rxInfo->mbuf == nullptr)) {
		ENA_ERROR_LOG_EXT(_controller, "NULL mbuf in rx_info\n");
		return nullptr;
	}

	buf = 0;
	len = _enaBufs[buf].len;
	reqID = _enaBufs[buf].req_id;
	if (unlikely(!validateReqID(reqID)))
		return nullptr;

	rxInfo = &_bufferInfo[reqID];

	ENA_DEBUG_LOG_EXT(_controller, "rx_info %p, mbuf %p, paddr %lld\n",
		rxInfo, rxInfo->mbuf, (uintmax_t) rxInfo->enaBuf.paddr);

	mbuf = rxInfo->mbuf;
	mbuf_setlen(mbuf, len);
	totalSize += len;

	ENA_DEBUG_LOG_EXT(_controller, "rx mbuf 0x%p, flags=0x%x, len: %d\n",
		mbuf, mbuf_flags(mbuf), (int) mbuf_pkthdr_len(mbuf));

	rxInfo->mbuf = nullptr;
	_freeIDs[ntc] = reqID;
	advanceIndex(ntc);

	// While we have more than 1 descriptors for one rcvd packet, append
	// other mbufs to the main one
	while (--descs) {
		buf++;
		len = _enaBufs[buf].len;
		reqID = _enaBufs[buf].req_id;
		if (unlikely(!validateReqID(reqID))) {
			_controller->freePacket(mbuf);
			return nullptr;
		}

		ENA_DEBUG_LOG_EXT(_controller, "Rx - next decsriptor with len %d\n", len);

		rxInfo = &_bufferInfo[reqID];

		if (unlikely(rxInfo->mbuf == nullptr)) {
			ENA_ERROR_LOG_EXT(_controller, "NULL mbuf in rx_info\n");
			// If one of the required mbufs was not allocated yet,
			// we can break there.
			// All earlier used descriptors will be reallocated
			// later and not used mbufs can be reused.
			// The nextToClean pointer will not be updated in case
			// of an error, so caller should advance it manually
			// in error handling routine to keep it up to date
			// with hw ring.
			_controller->freePacket(mbuf);
			return nullptr;
		}
		mbuf_setlen(rxInfo->mbuf, len);
		totalSize += len;

		mbuf = mbuf_concatenate(mbuf, rxInfo->mbuf);

		ENA_DEBUG_LOG_EXT(_controller, "Rx mbuf appended. len %ld\n",
			      mbuf_pkthdr_len(mbuf));

		rxInfo->mbuf = nullptr;

		_freeIDs[ntc] = reqID;
		advanceIndex(ntc);
	}

	mbuf_pkthdr_setlen(mbuf, totalSize);
	_nextToClean = ntc;

	return mbuf;
}

const ENARxStats * ENARingRx::getRxStats() const
{
	return &_rxStats;
}

/***********************************************************
 * ENAQueue definition
 ***********************************************************/

OSDefineMetaClassAndStructors(ENAQueue, OSObject)

ENAQueue * ENAQueue::withConfig(ENA *controller,
				ENARingTx::TxConfig *txConfig,
				ENARingRx::RxConfig *rxConfig)
{
	ENAQueue *queue = new ENAQueue;

	if (queue != nullptr) {
		if (!queue->init(controller, txConfig, rxConfig)) {
			queue->release();
			queue = nullptr;
		}
	}

	return queue;
}

bool ENAQueue::init(ENA *controller, ENARingTx::TxConfig *txConfig, ENARingRx::RxConfig *rxConfig)
{
	if (!OSObject::init())
		return false;

	_txRing = ENARingTx::withConfig(controller, txConfig);
	if (_txRing == nullptr)
		return false;

	_rxRing = ENARingRx::withConfig(controller, rxConfig);
	if (_rxRing == nullptr)
		return false;

	ENATask::Action action = OSMemberFunctionCast(
		ENATask::Action,
		this,
		&ENAQueue::handleInterrupt);
	_ioTask = ENATask::withAction(this, action);
	if (_ioTask == nullptr)
		return false;

	_qid = txConfig->qid;
	_controller = controller;

	return true;
}

void ENAQueue::free()
{
	SAFE_RELEASE(_ioTask);

	SAFE_RELEASE(_rxRing);
	SAFE_RELEASE(_txRing);

	OSObject::free();
}

bool ENAQueue::enable()
{
	if (!_txRing->enable()) {
		ENA_ERROR_LOG_EXT(_controller, "Failed to enable Tx ring\n");
		return false;
	}

	if (!_rxRing->enable()) {
		ENA_ERROR_LOG_EXT(_controller, "Failed to enable Rx ring\n");
		return false;
	}

	if (!enableIOInterruptHandler()) {
		ENA_ERROR_LOG_EXT(_controller, "Failed to enable IO interrupt handlers\n");
		return false;
	}

	return true;
}

void ENAQueue::disable()
{
	disableIOInterruptHandler();
	_txRing->disable();
	_rxRing->disable();
}

ENARingTx * ENAQueue::getTxRing()
{
	return _txRing;
}

ENARingRx * ENAQueue::getRxRing()
{
	return _rxRing;
}

UInt16 ENAQueue::getID()
{
	return _qid;
}

void ENAQueue::unmaskIOInterrupt(UInt32 rxDelay, UInt32 txDelay)
{
	struct ena_com_io_cq *ioCQ;
	struct ena_eth_io_intr_reg intrReg;
	UInt16 ioQID;

	ioQID = ENA_IO_TXQ_IDX(_qid);
	ioCQ = &_controller->getENADevice()->io_cq_queues[ioQID];

	ena_com_update_intr_reg(&intrReg, rxDelay, txDelay, true);
	ena_com_unmask_intr(ioCQ, &intrReg);
}

// This function should be called only from handleMainInterrupt()
void ENAQueue::interruptOccurred()
{
	_ioTask->callTask();
}

void ENAQueue::handleInterrupt()
{
	ENA_DEBUG_LOG_EXT(_controller, "ENAQueue::handleInterrupt for queue %d\n", _qid);

	for (int i = 0; i < ENA_CLEANUP_BUDGET; ++i) {
		if (unlikely(!_controller->testFlag(ENA_FLAG_DEV_UP)))
			return;

		int rxc = _rxRing->interruptProcess();
		int txc = _txRing->interruptProcess();

		// Stay in the loop only if at least one path depleted its budget
		if (rxc != _rxRing->getPacketCleanupBudget()  &&
		    txc != _txRing->getPacketCleanupBudget())
			break;
	}

	_controller->handleInterruptCompleted(this);
}

bool ENAQueue::enableIOInterruptHandler()
{
	_ioTask->enable();

	return true;
}

void ENAQueue::disableIOInterruptHandler()
{
	_ioTask->disable();
}
