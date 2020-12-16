// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENA.hpp"

#include "ENALogs.hpp"
#include "ENAProperties.hpp"

#include <sys/sysctl.h>

#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <libkern/OSKextLib.h>

#include <IOKit/IOBufferMemoryDescriptor.h>

static ENAProperties enaProperties;

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(ENA, IOEthernetController)

// Define the driver's superclass.
#define super IOEthernetController

int ena_log_level = 0x3;

bool ENA::init(OSDictionary *properties)
{
	ENA_DEBUG_LOGC("init() ----->\n");

	// Set _devName to 0 length string to use default getName() until PCI
	// address of the device will be available.
	_devName[0] = '\0';

	if (!super::init(properties)) {
		ENA_ERROR_LOGC("Init of the super class failed\n");
		goto error;
	}

	_globalLock = IOLockAlloc();
	if (_globalLock == nullptr) {
		ENA_ERROR_LOGC("Failed to alloc global lock\n");
		goto error;
	}

	_inputQueueLock = IOLockAlloc();
	if (_inputQueueLock == nullptr) {
		ENA_ERROR_LOGC("Failed to alloc input queue lock\n");
		goto error;
	}

	_hashFunction = ENAToeplitzHash::withKey(hashFunctionKey, sizeof(hashFunctionKey));
	if (_hashFunction == nullptr) {
		ENA_ERROR_LOG("Failed to allocate hash function\n");
		goto error;
	}

	_keepAliveTimeout.setSeconds(ENA_DEVICE_KALIVE_TIMEOUT);
	_missingTxCompletionTimeout.setSeconds(ENA_TX_TIMEOUT);
	_missingTxCompletionThreshold = ENA_MAX_NUM_OF_TIMEOUTED_PACKETS;

	initAtomicStats((AtomicStat *)&_hwStats, ENAHWStatsNum);

	ENA_DEBUG_LOGC("init() <-----\n");
	return true;

error:
	ENA_DEBUG_LOGC("init() <-----\n");
	return false;
}

void ENA::free()
{
	ENA_DEBUG_LOGC("free() ----->\n");

	if (_globalLock != nullptr) {
		IOLockLock(_globalLock);
		ENA_DEBUG_LOGC("free() lock acquired\n");
	}

	// Disable AENQ timer in case it's enabled before releasing it
	if ((_aenqTimerEvent != nullptr) && _aenqTimerEvent->isEnabled())
		_aenqTimerEvent->disable();

	// Release resources initialized in start()
	SAFE_RELEASE(_resetTask);
	SAFE_RELEASE(_aenqTimerEvent);
	SAFE_RELEASE(_aenqWorkLoop);
	SAFE_RELEASE(_timerEvent);
	SAFE_RELEASE(_mediumAuto);
	SAFE_RELEASE(_queues);

	releaseProperties();

	if (testFlag(ENA_FLAG_DEV_INIT)) {
		ena_com_dev_reset(_enaDev,
				  testFlag(ENA_FLAG_DEV_RUNNING)
				      ? ENA_REGS_RESET_NORMAL
				      : ENA_REGS_RESET_INIT_ERR);

		freeMainInterrupt();

		ena_com_abort_admin_commands(_enaDev);
		ena_com_wait_for_abort_completion(_enaDev);
		ena_com_admin_destroy(_enaDev);
		ena_com_mmio_reg_read_request_destroy(_enaDev);
		ena_com_rss_destroy(_enaDev);
	}
	clearFlag(ENA_FLAG_DEV_RUNNING);
	clearFlag(ENA_FLAG_DEV_INIT);

	if (_enaDev != nullptr) {
		IODelete(_enaDev, struct ena_com_dev, 1);
		_enaDev = nullptr;
	}

	SAFE_RELEASE(_netif);
	SAFE_RELEASE(_txQueue);

	releasePCIResources();
	if (_pciNub != nullptr) {
		if (_pciNub->isOpen())
			_pciNub->close(this);
		_pciNub->release();
	}
	SAFE_RELEASE(_workLoop);

	SAFE_RELEASE(_hashFunction);
	if (_inputQueueLock != nullptr) {
		IOLockFree(_inputQueueLock);
		_inputQueueLock = nullptr;
	}

	// Release resources configured in init()
	if (_globalLock != nullptr) {
		IOLockUnlock(_globalLock);
		IOLockFree(_globalLock);
		_globalLock = nullptr;
	}

	super::free();
	ENA_DEBUG_LOGC("free() <-----\n");
}

bool ENA::start(IOService *provider)
{
	IOTimerEventSource::Action timerService;
	IOTimerEventSource::Action aenqTimerService;
	ENATask::Action resetAction;
	struct ena_com_dev_get_features_ctx features;
	UInt32 ioQueueNum, queueSize;
	UInt16 txSglSize, rxSglSize;
	bool wdState;

	ENA_DEBUG_LOG("start() ----->\n");

	IOLockLock(_globalLock);
	ENA_DEBUG_LOG("start() lock acquired\n");

	// Save reference to the provider and use RTTI to check if this is PCI nub
	_pciNub = OSDynamicCast(IOPCIDevice, provider);
	if (_pciNub == nullptr) {
		ENA_ERROR_LOG("Failed to cast provider\n");
		goto error_unlock;
	}
	// From now the nub pointer belongs to the driver and must
	// be released by the driver
	_pciNub->retain();

	// Append bus:dev.fun to the original class name.
	snprintf(_devName, sizeof(_devName), "%s %02x:%02x.%02x", super::getName(),
		 _pciNub->getBusNumber(), _pciNub->getDeviceNumber(), _pciNub->getFunctionNumber());

	if (!super::start(provider)) {
		ENA_ERROR_LOG("Start of the super class failed\n");
		goto error_unlock;
	}

	if (!_pciNub->open(this)) {
		ENA_ERROR_LOG("Cannot open PCI device\n");
		goto error_stop;
	}

	// Driver specific resources
	_enaDev = IONew(struct ena_com_dev, 1);
	if (_enaDev == nullptr) {
		ENA_ERROR_LOG("Failed to allocate memory for the ena_com device\n");
		goto error_stop;
	}
	memset(_enaDev, 0, sizeof(*_enaDev));

	_enaDev->dmadev = _pciNub;

	initPCIConfigSpace();
	if (!allocatePCIResources()) {
		ENA_ERROR_LOG("Failed to allocate PCI resources\n");
		goto error_stop;
	}

	_enaDev->reg_bar = (u8 *)_regMap->getVirtualAddress();
	if (!_enaDev->reg_bar) {
		ENA_ERROR_LOG("Failed to remap regs bar\n");
		goto error_stop;
	}

	if(!initDevice(&features, wdState)) {
		ENA_ERROR_LOG("Failed to allocate resources for IO queues\n");
		goto error_stop;
	}
	setFlag(ENA_FLAG_DEV_INIT);

	_enaDev->tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_HOST;
	_enaDev->intr_moder_tx_interval = ENA_INTR_INITIAL_TX_INTERVAL_USECS;

	ioQueueNum = calcIOQueueNum(features);
	queueSize = calcIOQueueSize(features, &txSglSize, &rxSglSize);

	if ((queueSize == 0) || (ioQueueNum == 0)) {
		ENA_ERROR_LOG("Invalid queues configuration: queue size=%u; number of queues=%u\n",
			      queueSize, ioQueueNum);
		goto error_stop;
	}

	memcpy(_macAddr.bytes, features.dev_attr.mac_addr, kIOEthernetAddressSize);

	setSupportedOffloads(features.offload);

	// Driver can handle Tx mbufs which are not physically contiguous
	_featuresMask |= kIONetworkFeatureMultiPages;

	ENA_DEBUG_LOG("Features mask: 0x%x, Tx checksum mask: 0x%x, Rx checksum mask: 0x%x\n",
		  _featuresMask, _txChecksumMask, _rxChecksumMask);

	_maxMTU = features.dev_attr.max_mtu;

	_numQueues = ioQueueNum;
	_queueSize = queueSize;
	_maxTxSglSize = txSglSize;
	_maxRxSglSize = rxSglSize;
	if (wdState)
		setFlag(ENA_FLAG_WATCHDOG_ACTIVE);

	if (!allocateIOQueues()) {
		ENA_ERROR_LOG("Failed to allocate the IO queues\n");
		goto error_stop;
	}

	if (!setupMainInterrupt()) {
		ENA_ERROR_LOG("Setup main interrupt failed\n");
		goto error_stop;
	}

	if (!requestMainInterrupt()) {
		ENA_ERROR_LOG("Setting main interrupt failed\n");
		goto error_stop;
	}

	if (!rssInitDefault()) {
		ENA_ERROR_LOG("Failed to initialize RSS\n");
		goto error_stop;
	}

	// ENA can be used only with ENAParallelOutputQueue for multiqueue support
	_txQueue = createParallelOutputQueue();
	if (_txQueue == nullptr) {
		ENA_ERROR_LOG("Failed to create parallel output queue\n");
		goto error_stop;
	}

	_mediumAuto = IONetworkMedium::medium(kIOMediumEthernetAuto, 0);
	if (_mediumAuto == nullptr) {
		ENA_ERROR_LOG("Failed to create auto negotiated medium\n");
		goto error_stop;
	}

	// ENAParallelOutputQueue can contain multiple subqueues. As they can hold a lot of resources,
	// they should be activated as late as possible with minimal required kernel memory.
	if (!_txQueue->setActiveSubQueues(ioQueueNum)) {
		ENA_ERROR_LOG("Failed to activate parallel output queue with %d subqueues\n", ioQueueNum);
		goto error_stop;
	}

	_pciNub->close(this);

	timerService = OSMemberFunctionCast(IOTimerEventSource::Action,
					    this,
					    &ENA::timerService);
	_timerEvent = IOTimerEventSource::timerEventSource(
		kIOTimerEventSourceOptionsPriorityKernelHigh,
		this,
		timerService);
	if (_timerEvent == nullptr) {
		ENA_ERROR_LOG("Failed to create timer service\n");
		goto error_stop;
	}
	// Use the controller work loop for timer event synchronization
	_workLoop->addEventSource(_timerEvent);

	_aenqWorkLoop = IOWorkLoop::workLoop();
	if (_aenqWorkLoop == nullptr) {
		ENA_ERROR_LOG("Failed to create auto admin workloop\n");
		goto error_stop;
	}

	aenqTimerService = OSMemberFunctionCast(IOTimerEventSource::Action,
						this,
						&ENA::aenqTimerService);

	_aenqTimerEvent = IOTimerEventSource::timerEventSource(
		kIOTimerEventSourceOptionsPriorityKernelHigh,
		this,
		aenqTimerService);
	if (_aenqTimerEvent == nullptr) {
		ENA_ERROR_LOG("Failed to create admin timer service\n");
		goto error_stop;
	}

	_aenqWorkLoop->addEventSource(_aenqTimerEvent);

	resetAction = OSMemberFunctionCast(ENATask::Action,
					   this,
					   &ENA::resetAction);
	_resetTask = ENATask::withAction(this, resetAction);
	if (_resetTask == nullptr) {
		ENA_ERROR_LOG("Failed to create reset task\n");
		goto error_stop;
	}

	_lastKeepAliveTime.setNow();

	ena_com_admin_aenq_enable(_enaDev);

	setLinkStatus(kIONetworkLinkValid);

	if (!attachInterface((IONetworkInterface **)&_netif, true)) {
		ENA_ERROR_LOG("Cannot attach Ethernet interface\n");
		goto error_stop;
	}

	_resetTask->enable();

	setFlag(ENA_FLAG_DEV_RUNNING);

	_aenqTimerEvent->enable();
	_aenqTimerEvent->setTimeoutMS(ENA_AENQ_TIMER_SERVICE_TIMEOUT_MS);

	ENA_LOG("Device has been started, driver version: %s\n", OSKextGetCurrentVersionString());

	IOLockUnlock(_globalLock);

	ENA_DEBUG_LOG("start() <-----\n");
	return true;

error_stop:
	super::stop(provider);
error_unlock:
	IOLockUnlock(_globalLock);
	ENA_DEBUG_LOG("start() <-----\n");
	return false;
}

void ENA::stop(IOService *provider)
{
	ENA_DEBUG_LOG("stop() ----->\n");

	// As the stop() is only being called after start() finished
	// successfully, it is safe to disable reset task in order to prevent
	// deadlocks with the global lock inside the free().
	// In a situation when the _resetTask would still be running and in the
	// meantime the free() would take the global lock, the
	// SAFE_RELEASE(_resetTask) could cause a dead lock because it needs to
	// disable the reset task first, which needs to take an internal lock.
	// The internal lock is released when the currently running method is
	// being finished.
	_resetTask->disable();

	IOLockLock(_globalLock);
	ENA_DEBUG_LOG("stop() lock acquired\n");

	super::stop(provider);

	IOLockUnlock(_globalLock);
	ENA_DEBUG_LOG("stop() <-----\n");
}

/*
 * Note: all calls to this functions should be protected by holding the common
 * lock, for example: _globalLock.
 */
IOReturn ENA::enableLocked(IONetworkInterface *netif)
{
	IOReturn ret;

	if (!_pciNub->open(this)) {
		ENA_ERROR_LOG("Cannot open PCI device\n");
		ret = kIOReturnError;
		goto error;
	}

	if (!enableAllIOQueues()) {
		ENA_ERROR_LOG("Failed to enable IO queues\n");
		ret = kIOReturnError;
		goto err_close_pci;
	}

	if (!rssConfigure()) {
		ENA_ERROR_LOG("Failed to configure RSS\n");
		ret = kIOReturnError;
		goto err_disable_queues;
	}

	if (!_txQueue->setCapacity(_queueSize) || !_txQueue->start()) {
		ENA_ERROR_LOG("Failed to start Tx output queue\n");
		ret = kIOReturnError;
		goto err_disable_queues;
	}

	setFlag(ENA_FLAG_DEV_UP);

	unmaskAllIOInterrupts();

	if (testFlag(ENA_FLAG_LINK_UP))
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, _mediumAuto);


	_lastMonitoredQueueID = 0;

	// Activate timer service only if the device is running.
	// If this flag is not set, it means that the driver is being
	// reset and timer service will be activated afterwards.
	if (testFlag(ENA_FLAG_DEV_RUNNING)) {
		_timerEvent->enable();
		_timerEvent->setTimeoutMS(ENA_TIMER_SERVICE_TIMEOUT_MS);
	}

	return kIOReturnSuccess;

err_disable_queues:
	disableAllIOQueues();
err_close_pci:
	_pciNub->close(this);
error:
	return ret;
}

IOReturn ENA::enable(IONetworkInterface *netif)
{
	IOReturn ret;

	ENA_DEBUG_LOG("enable() ----->\n");
	IOLockLock(_globalLock);
	ENA_DEBUG_LOG("enable() lock acquired\n");

	ret = enableLocked(netif);

	ENA_LOG("Device has been enabled\n");

	IOLockUnlock(_globalLock);
	ENA_DEBUG_LOG("enable() <-----\n");

	return ret;
}

/*
 * Note: all calls to this functions should be protected by holding the common
 * lock, for example: _globalLock.
 */
IOReturn ENA::disableLocked(IONetworkInterface *netif)
{
	int rc;

	_timerEvent->disable();

	setLinkStatus(kIONetworkLinkValid);

	_txQueue->stop();
	_txQueue->flush();

	if (testFlag(ENA_FLAG_TRIGGER_RESET)) {
		rc = ena_com_dev_reset(_enaDev, _resetReason);
		if (rc != 0)
			ENA_ERROR_LOG("Device reset failed\n");
		ena_com_set_admin_running_state(_enaDev, false);
	}

	clearFlag(ENA_FLAG_DEV_UP);

	disableAllIOQueues();

	_pciNub->close(this);

	return kIOReturnSuccess;
}

IOReturn ENA::disable(IONetworkInterface *netif)
{
	IOReturn ret;

	ENA_DEBUG_LOG("disable() ----->\n");
	IOLockLock(_globalLock);
	ENA_DEBUG_LOG("disable() lock acquired\n");

	ret = disableLocked(netif);

	ENA_LOG("Device has been disabled\n");

	IOLockUnlock(_globalLock);
	ENA_DEBUG_LOG("disable() <-----\n");

	return ret;
}

IOReturn ENA::setMulticastMode(bool active)
{
	// ENA doesn't need to configure the multicast - but success have to been
	// returned in order to correctly load the interface.
	return kIOReturnSuccess;
}

IOReturn ENA::setPromiscuousMode(bool active)
{
	// There is nothing to do in the HW for promiscuous mode, but this method
	// have to return success in order to correctly load the interface.
	return kIOReturnSuccess;
}

IOReturn ENA::setMaxPacketSize(UInt32 maxSize)
{
	int rc, mtu;

	mtu = maxSize - kIOEthernetCRCSize - sizeof(struct ether_header);

	rc = ena_com_set_dev_mtu(_enaDev, mtu);
	if (unlikely(rc != 0)) {
		ENA_ERROR_LOG("Failed to set MTU to %d\n", mtu);
		return kIOReturnError;
	}

	ENA_DEBUG_LOG("Set MTU to %d\n", mtu);
	return kIOReturnSuccess;
}

IOReturn ENA::getMaxPacketSize(UInt32 *maxSize) const
{
	ENA_DEBUG_LOG("getMaxPacketSize()\n");

	*maxSize = _maxMTU + kIOEthernetCRCSize + sizeof(struct ether_header);

	return kIOReturnSuccess;
}

bool ENA::configureInterface(IONetworkInterface *netif)
{
	IONetworkData *data;
	if (!super::configureInterface(netif))
		return false;

	data = netif->getNetworkData(kIONetworkStatsKey);
	if (!data || !(_netStats = (IONetworkStats *)data->getBuffer())) {
		ENA_ERROR_LOG("There are no network statistics\n");
		return false;
	}

	return true;
}

bool ENA::handleMainInterrupt(IOFilterInterruptEventSource *src)
{
	// Because of Darwin limitations in terms of MSI-x handlers (only single
	// one is supported), everything must be handled within the one handler.
	// This routine is executing async tasks - IO queues.
	ENAQueue *queue;

	atomic_store(&_interruptHandlersRemaining, _numQueues);

	if (likely(testFlag(ENA_FLAG_DEV_UP))) {
		for (int i = 0; i < _numQueues; ++i) {
			queue = OSDynamicCast(ENAQueue, _queues->getObject(i));
			queue->interruptOccurred();
		}
	}

	// Return false, as we've done all processing there and scheduled our own tasks.
	return false;
}

IOReturn ENA::getHardwareAddress(IOEthernetAddress *addrP)
{
	*addrP = _macAddr;
	return kIOReturnSuccess;
}

bool ENA::createWorkLoop()
{
	// Create new work loop for the driver instance. Not creating new work
	// loop will result in using global work loop of the IOKit.
	_workLoop = IOWorkLoop::workLoop();
	return (_workLoop != nullptr);
}

IOWorkLoop *ENA::getWorkLoop() const
{
	// Get work loop specific for the driver instance,
	// instead of the global one
	return _workLoop;
}

IOOutputQueue *ENA::getOutputQueue() const
{
	return _txQueue;
}

struct ena_com_dev * ENA::getENADevice()
{
	return _enaDev;
}

IOPCIDevice * ENA::getProvider()
{
	return _pciNub;
}

IOEthernetInterface * ENA::getEthernetInterface()
{
	return _netif;
}

IONetworkStats * ENA::getNetworkStats()
{
	return _netStats;
}

bool ENA::initEventSources(IOService *provider)
{
	if (_workLoop == nullptr)
		return false;
	return true;
}

bool ENA::allocateIOQueues()
{
	ENAQueue *queue;
	ENARingTx::TxConfig txConfig;
	ENARingRx::RxConfig rxConfig;
	bool success = true;

	_queues = OSArray::withCapacity(_numQueues);
	if (_queues == nullptr) {
		ENA_DEBUG_LOG("Cannot allocate array for the IO queues\n");
		return false;
	}

	// Configuration for Tx queue
	txConfig.memQueueType = _enaDev->tx_mem_queue_type;
	txConfig.queueSize = _queueSize;
	txConfig.maxSegmentsNumber = _maxTxSglSize;
	txConfig.maxSegmentSize = txBufMaxSize;
	txConfig.packetCleanupBudget = ENA_TX_BUDGET;

	// Configuration for Rx queue
	rxConfig.memQueueType = ENA_ADMIN_PLACEMENT_POLICY_HOST;
	rxConfig.queueSize = _queueSize;
	rxConfig.maxSegmentsNumber = _maxRxSglSize;
	rxConfig.maxSegmentSize = MBIGCLBYTES;
	rxConfig.packetCleanupBudget = ENA_RX_BUDGET;

	for (size_t i = 0; i < _numQueues; i++) {
		// Set the IDs for both queues
		txConfig.qid = i;
		rxConfig.qid = i;
		queue = ENAQueue::withConfig(this, &txConfig, &rxConfig);
		if (queue == nullptr) {
			ENA_DEBUG_LOG("Cannot allocate queue\n");
			success = false;
			break;
		}

		if (!_queues->setObject(queue)) {
			queue->release();
			ENA_DEBUG_LOG("Cannot add queue to array\n");
			success = false;
			break;
		}

		// As OSArray retains the objects, we need to release the queue.
		queue->release();
	}

	// This will also release all queues currently held in the OSArray.
	if (!success)
		SAFE_RELEASE(_queues);

	return success;
}

UInt32 ENA::calcIOQueueNum(const struct ena_com_dev_get_features_ctx &features)
{
	UInt32 ioTxSQNum, ioTxCQNum, ioRxNum, ioQueueNum;
	UInt32 ncpu = 0;
	size_t ncpuSize = sizeof(ncpu);
	int rv;

	rv = sysctlbyname("hw.ncpu", &ncpu, &ncpuSize, nullptr, 0);
	assert(rv == 0);

	if (_enaDev->supported_features & BIT(ENA_ADMIN_MAX_QUEUES_EXT)) {
		ioRxNum = min(features.max_queue_ext.max_queue_ext.max_rx_sq_num,
			      features.max_queue_ext.max_queue_ext.max_rx_cq_num);
		ioTxSQNum = features.max_queue_ext.max_queue_ext.max_tx_sq_num;
		ioTxCQNum = features.max_queue_ext.max_queue_ext.max_tx_cq_num;
	} else {
		ioTxSQNum = features.max_queues.max_sq_num;
		ioTxCQNum = features.max_queues.max_cq_num;
		ioRxNum = min(ioTxSQNum, ioTxCQNum);
	}

	if (_enaDev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
		ioTxSQNum = features.llq.max_llq_num;
	}

	ioQueueNum = min(ENA_MAX_NUM_IO_QUEUES, ncpu);
	ioQueueNum = min(ioQueueNum, ioRxNum);
	ioQueueNum = min(ioQueueNum, ioTxSQNum);
	ioQueueNum = min(ioQueueNum, ioTxCQNum);
	ioQueueNum = min(ioQueueNum, maxSupportedIOQueues);

	return ioQueueNum;
}

UInt32 ENA::calcIOQueueSize(const struct ena_com_dev_get_features_ctx &features,
			    UInt16 *txSglSize,
			    UInt16 *rxSglSize)
{
	UInt32 queueSize = ENA_DEFAULT_RING_SIZE;

	if (_enaDev->supported_features & BIT(ENA_ADMIN_MAX_QUEUES_EXT)) {
		const struct ena_admin_queue_ext_feature_fields *max_queue_ext =
		&features.max_queue_ext.max_queue_ext;
		queueSize = min(queueSize, max_queue_ext->max_rx_cq_depth);
		queueSize = min(queueSize, max_queue_ext->max_rx_sq_depth);
		queueSize = min(queueSize, max_queue_ext->max_tx_cq_depth);

		if (_enaDev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
			queueSize = min(queueSize, features.llq.max_llq_depth);
		} else {
			queueSize = min(queueSize, max_queue_ext->max_tx_sq_depth);
		}

		*txSglSize = min(enaPktMaxBufs,
				 features.max_queue_ext.max_queue_ext.max_per_packet_tx_descs);
		*rxSglSize = min(enaPktMaxBufs,
				 features.max_queue_ext.max_queue_ext.max_per_packet_rx_descs);
	} else {
		const struct ena_admin_queue_feature_desc *max_queues =
		&features.max_queues;
		queueSize = min(queueSize, max_queues->max_cq_depth);
		if (_enaDev->tx_mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV) {
			queueSize = min(queueSize, features.llq.max_llq_depth);
		}
		else {
			queueSize = min(queueSize, max_queues->max_sq_depth);
		}

		*txSglSize = min(enaPktMaxBufs,
				 features.max_queues.max_packet_tx_descs);
		*rxSglSize = min(enaPktMaxBufs,
				 features.max_queues.max_packet_rx_descs);
	}

	// Round queue size to power of 2
	if (unlikely(queueSize == 0)) {
		ENA_DEBUG_LOG("Invalid queue size\n");
		return 0;
	} else {
		queueSize = 1U << (31 -__builtin_clz(queueSize));
	}

	return queueSize;
}

void ENA::setSupportedOffloads(const struct ena_admin_feature_offload_desc &offloads)
{
	if (offloads.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L3_CSUM_IPV4_MASK)
		_txChecksumMask |= kChecksumIP;

	if (offloads.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_PART_MASK)
		_txChecksumMask |= kChecksumTCPNoPseudoHeader | kChecksumUDPNoPseudoHeader;

	if (offloads.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_FULL_MASK)
		_txChecksumMask |= kChecksumTCP | kChecksumUDP;

	if (offloads.tx & ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_FULL_MASK)
		_txChecksumMask |= kChecksumTCPIPv6 | kChecksumUDPIPv6;

	if (offloads.rx_supported & ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L3_CSUM_IPV4_MASK)
		_rxChecksumMask |= kChecksumIP;

	if (offloads.rx_supported & ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV4_CSUM_MASK)
		_rxChecksumMask |= kChecksumTCP | kChecksumUDP;

	if (offloads.rx_supported & ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV6_CSUM_MASK)
		_rxChecksumMask |= kChecksumTCPIPv6 | kChecksumUDPIPv6;

	// Just for a workaround, we would enable TX IPv4 and TCP Checksum offloads as we know the HW supports it
	// Till we support ENAv2 which also enable supporting RX CSUM, we advertise supporting RX CSUM but will set that the
	// offloading was not performed for each packet.
	// We have to so since Mac OS doesn't allow to support only TX or RX checksum.
	_txChecksumMask |= ENA_SUPPORTED_OFFLOADS;
	_rxChecksumMask |= ENA_SUPPORTED_OFFLOADS;
}

IOReturn ENA::getChecksumSupport(UInt32 *checksumMask, UInt32 checksumFamily, bool isOutput)
{
	if (checksumFamily != kChecksumFamilyTCPIP)
		return kIOReturnUnsupported;

	*checksumMask	= isOutput ? _txChecksumMask : _rxChecksumMask;

	return kIOReturnSuccess;
}

UInt32 ENA::getFeatures() const
{
	return _featuresMask;
}

void ENA::initPCIConfigSpace()
{
	// PCI configuration
	UInt16 cmd = _pciNub->configRead16(kIOPCIConfigCommand);
	cmd |= (kIOPCICommandBusMaster |
		kIOPCICommandMemorySpace |
		kIOPCICommandMemWrInvalidate); // Tell the bus master to write minimum of one cache line
	cmd &= ~kIOPCICommandIOSpace;

	_pciNub->configWrite16(kIOPCIConfigCommand, cmd);
	ENA_DEBUG_LOG("PCI CMD = %04x\n", _pciNub->configRead16(kIOPCIConfigCommand));
}

bool ENA::allocatePCIResources()
{
	// Map BAR with registers
	_regMap = _pciNub->mapDeviceMemoryWithRegister(ENA_REG_BAR);
	if (_regMap == nullptr) {
		ENA_ERROR_LOG("PCI BAR@%x mapping error\n", ENA_REG_BAR);
		return false;
	}

	_memMap = nullptr;

	return true;
}

void ENA::releasePCIResources()
{
	SAFE_RELEASE(_regMap);
	SAFE_RELEASE(_memMap);
}

int ENA::configHostInfo()
{
	struct ena_admin_host_info *hostInfo;
	size_t nKernelVersion, nKernelVersionStr, nOSDistStr, nNumCPUs;
	UInt32 numCPUs;
	int rc, rv;
	int driverVersion[3];

	// Set sizes of system control getters
	nNumCPUs = sizeof(numCPUs);
	nKernelVersion = sizeof(hostInfo->kernel_ver);
	nKernelVersionStr = sizeof(hostInfo->kernel_ver_str);
	nOSDistStr = sizeof(hostInfo->os_dist_str);

	// Allocate only the host info
	rc = ena_com_allocate_host_info(_enaDev);
	if (unlikely(rc)) {
		ENA_ERROR_LOG("Cannot set host info, err: %d\n", rc);
		return rc;
	}

	hostInfo = _enaDev->host_attr.host_info;

	hostInfo->os_type = ENA_ADMIN_OS_MACOS;

	// Get Os version
	rv = sysctlbyname("kern.osproductversion", &hostInfo->os_dist_str, &nOSDistStr, nullptr, 0);
	assert(rv == 0);
	hostInfo->os_dist = 0; // os_dist is not relevant as there are no distributions in Mac OS

	// Get Kernel version
	rv = sysctlbyname("kern.osrelease", &hostInfo->kernel_ver_str, &nKernelVersionStr, nullptr, 0);
	assert(rv == 0);
	rv = sysctlbyname("kern.osrevision", &hostInfo->kernel_ver, &nKernelVersion, nullptr, 0);
	assert(rv == 0);

	// Get driver version
	sscanf(OSKextGetCurrentVersionString(), "%d.%d.%d", &driverVersion[0], &driverVersion[1], &driverVersion[2]);
	hostInfo->driver_version =
			(driverVersion[0]) |
			(driverVersion[1] << ENA_ADMIN_HOST_INFO_MINOR_SHIFT) |
			(driverVersion[2] << ENA_ADMIN_HOST_INFO_SUB_MINOR_SHIFT);

	hostInfo->bdf = (_pciNub->getBusNumber() << ENA_ADMIN_HOST_INFO_BUS_SHIFT) |
			(_pciNub->getDeviceNumber() << ENA_ADMIN_HOST_INFO_DEVICE_SHIFT) |
			_pciNub->getFunctionNumber();

	// Get number of CPUs
	rv = sysctlbyname("hw.ncpu", &numCPUs, &nNumCPUs, nullptr, 0);
	assert(rv == 0);
	hostInfo->num_cpus = (UInt16)numCPUs;

	rc = ena_com_set_host_attributes(_enaDev);
	if (unlikely(rc)) {
		if (rc == ENA_COM_UNSUPPORTED)
			ENA_ERROR_LOG("Device doesn't support setting host info\n");
		else
			ENA_ERROR_LOG("Cannot set host attributes\n");
		goto err;
	}

	return rc;

err:
	ena_com_delete_host_info(_enaDev);

	return rc;
}

bool ENA::initDevice(struct ena_com_dev_get_features_ctx *features, bool &wdState)
{
	UInt32 aenqGroups;
	int rc;
	bool  readlessSupported;

	rc = ena_com_mmio_reg_read_request_init(_enaDev);
	if (unlikely(rc)) {
		ENA_ERROR_LOG("Failed to init mmio read less, err: %d\n", rc);
		return false;
	}

	readlessSupported = !(_pciNub->configRead8(kIOPCIConfigRevisionID) &
			      ENA_MMIO_DISABLE_REG_READ);
	ena_com_set_mmio_read_mode(_enaDev, readlessSupported);

	rc = ena_com_dev_reset(_enaDev, ENA_REGS_RESET_NORMAL);
	if (rc) {
		ENA_ERROR_LOG("Can not reset device: err: %d\n", rc);
		goto err_mmio_read_less;
	}

	rc = ena_com_validate_version(_enaDev);
	if (rc) {
		ENA_ERROR_LOG("Device version is too low, err: %d\n", rc);
		goto err_mmio_read_less;
	}

	rc = ena_com_admin_init(_enaDev, &aenqHandlers);
	if (rc) {
		ENA_ERROR_LOG("Can not initialize ena admin queue with device, err: %d\n", rc);
		goto err_mmio_read_less;
	}

	// To enable the msix interrupts the driver needs to know the number
	// of queues. So the driver uses polling mode to retrieve this
	// information
	ena_com_set_admin_polling_mode(_enaDev, true);

	rc = configHostInfo();
	if (rc) {
		ENA_ERROR_LOG("Cannot set host info, err: %d\n", rc);
		goto err_admin_init;
	}

	rc = ena_com_get_dev_attr_feat(_enaDev, features);
	if (rc) {
		ENA_ERROR_LOG("Cannot get attribute for ena device, err: %d\n", rc);
		goto err_host_info;
	}

	aenqGroups = BIT(ENA_ADMIN_FATAL_ERROR) |
		     BIT(ENA_ADMIN_WARNING) |
		     BIT(ENA_ADMIN_KEEP_ALIVE);
	aenqGroups &= features->aenq.supported_groups;

	rc = ena_com_set_aenq_config(_enaDev, aenqGroups);
	if (rc) {
		ENA_ERROR_LOG("Cannot configure aenq groups, err: %d\n", rc);
		goto err_host_info;
	}

	wdState = !!(aenqGroups & BIT(ENA_ADMIN_KEEP_ALIVE));

	return true;

err_host_info:
	ena_com_delete_host_info(_enaDev);
err_admin_init:
	ena_com_admin_destroy(_enaDev);
err_mmio_read_less:
	ena_com_mmio_reg_read_request_destroy(_enaDev);

	return false;
}

bool ENA::setupMainInterrupt()
{
	UInt16 deviceID;

	deviceID = _pciNub->configRead16(kIOPCIConfigDeviceID);

	// Select interrupt source based on device ID
	// as IOPCIMessagedInterruptController is the correct one to use
	// For 0x0ec2 --> ENA_MAIN_IRQ_PF_IDX
	// "IOInterruptControllers" = ("io-apic-0","IOPCIMessagedInterruptController")
	// For 0xec20 --> ENA_MAIN_IRQ_VF_IDX
	// "IOInterruptControllers" = ("IOPCIMessagedInterruptController")
	switch (deviceID) {
	case PCI_DEV_ID_ENA_PF:
		_mainIrq.source = ENA_MAIN_IRQ_PF_IDX;
		break;
	case PCI_DEV_ID_ENA_VF:
		_mainIrq.source = ENA_MAIN_IRQ_VF_IDX;
		break;
	default:
		ENA_ERROR_LOG("Unkown device ID 0x%x\n", deviceID);
		return false;
	}

	// Pointer to the controller->handleMainInterrupt function (not static one),
	// this pointer is associated with 'this' object.
	_mainIrq.handler = OSMemberFunctionCast(
		IOFilterInterruptAction,
		this,
		&ENA::handleMainInterrupt);

	atomic_init(&_interruptHandlersRemaining, 0);
	return true;
}

bool ENA::requestMainInterrupt()
{
	_mainIrq.workLoop = IOWorkLoop::workLoop();
	if (_mainIrq.workLoop == nullptr) {
		ENA_DEBUG_LOG("Cannot allocate work loop for the admin irq\n");
		return false;
	}

	_mainIrq.interruptSrc = IOFilterInterruptEventSource::filterInterruptEventSource(
		this,
		nullptr,
		_mainIrq.handler,
		_pciNub,
		_mainIrq.source);
	if (_mainIrq.interruptSrc == nullptr) {
		ENA_DEBUG_LOG("Failed to add irq event to the workloop: %d\n",
			  _mainIrq.source);
		_mainIrq.workLoop->release();
		_mainIrq.workLoop = nullptr;
		return false;
	}

	_mainIrq.workLoop->addEventSource(_mainIrq.interruptSrc);
	_mainIrq.interruptSrc->enable();

	return true;
}


void ENA::freeMainInterrupt()
{
	if (_mainIrq.workLoop != nullptr && _mainIrq.interruptSrc != nullptr)
		_mainIrq.workLoop->removeEventSource(_mainIrq.interruptSrc);
	SAFE_RELEASE(_mainIrq.interruptSrc);
	SAFE_RELEASE(_mainIrq.workLoop);
}

bool ENA::testFlag(ENAFlags flag)
{
	return _flags & (1 << flag);
}

void ENA::setFlag(ENAFlags flag)
{
	_flags |= 1<<flag;
}

void ENA::clearFlag(ENAFlags flag)
{
	_flags &= ~(1<<flag);
}

void ENA::setupTxChecksum(struct ena_com_tx_ctx &txCtx, mbuf_t m)
{
	struct ena_com_tx_meta &meta = txCtx.ena_meta;
	struct ether_header *etherHeader;
	struct ip *ip;
	struct tcphdr *tcpHeader;
	size_t mlen;
	UInt32 checksumDemand;
	UInt32 ipHeaderLen;
	UInt16 etherType;
	UInt8 etherHeaderLen;
	UInt8 *headerPointer;

	getChecksumDemand(m, kChecksumFamilyTCPIP, &checksumDemand);

	if (!(_txChecksumMask & checksumDemand)) {
		txCtx.meta_valid = false;
		return;
	}

	ENA_DEBUG_LOG("Tx checksum demand: 0x%x\n", checksumDemand);

	headerPointer = reinterpret_cast<UInt8 *>(mbuf_data(m));
	mlen = mbuf_len(m);

	etherHeader = reinterpret_cast<struct ether_header *>(headerPointer);
	if (etherHeader->ether_type == htons(ETHERTYPE_VLAN)) {
		etherHeaderLen = etherVLANHeaderLen;
		etherType = ntohs(*reinterpret_cast<UInt16 *>(headerPointer + etherTypeVLANOffset));
	} else {
		etherHeaderLen = ETHER_HDR_LEN;
		etherType = ntohs(etherHeader->ether_type);
	}

	// Advance the header pointer for L3 header
	if (etherHeaderLen == mlen) {
		m = mbuf_next(m);
		mlen = mbuf_len(m);
		headerPointer = reinterpret_cast<UInt8 *>(mbuf_data(m));
	} else {
		headerPointer += etherHeaderLen;
		mlen -= etherHeaderLen;
	}

	if (checksumDemand & kChecksumIP)
		txCtx.l3_csum_enable = true;

	if (etherType == ETHERTYPE_IP) {
		ip = reinterpret_cast<struct ip *>(headerPointer);
		ipHeaderLen = ip->ip_hl << 2;
		txCtx.l3_proto = ENA_ETH_IO_L3_PROTO_IPV4;
		if ((ip->ip_off & htons(IP_DF)) != 0)
			txCtx.df = 1;
	} else if (etherType == ETHERTYPE_IPV6) {
		ipHeaderLen = sizeof(struct ip6_hdr);
		txCtx.l3_proto = ENA_ETH_IO_L3_PROTO_IPV6;
	} else {
		txCtx.meta_valid = false;
		return;
	}

	// Advance the header pointer for L4 header
	if (ipHeaderLen == mlen) {
		m = mbuf_next(m);
		headerPointer = reinterpret_cast<UInt8 *>(mbuf_data(m));
	} else {
		headerPointer += ipHeaderLen;
	}

	if (checksumDemand & (kChecksumTCP | kChecksumUDP)) {
		txCtx.l4_csum_enable = 1;
	} else if (checksumDemand & (kChecksumTCPNoPseudoHeader | kChecksumUDPNoPseudoHeader)) {
		txCtx.l4_csum_partial = 1;
	}

	if (checksumDemand & (kChecksumTCP | kChecksumTCPNoPseudoHeader)) {
		tcpHeader = reinterpret_cast<struct tcphdr *>(headerPointer);
		txCtx.l4_proto = ENA_ETH_IO_L4_PROTO_TCP;
		meta.l4_hdr_len = tcpHeader->th_off;
	} else if (checksumDemand & (kChecksumUDP | kChecksumUDPNoPseudoHeader)) {
		txCtx.l4_proto = ENA_ETH_IO_L4_PROTO_UDP;
		meta.l4_hdr_len = sizeof(struct udphdr) / 4;
	}

	meta.l3_hdr_len = ipHeaderLen;
	meta.l3_hdr_offset = etherHeaderLen;
	txCtx.meta_valid = true;
}

void ENA::processRxChecksum(struct ena_com_rx_ctx &rxCtx, mbuf_t m)
{
	UInt32 result = 0U;
	UInt32 valid = 0U;

	// L3
	if (_rxChecksumMask & kChecksumIP) {
		if (rxCtx.l3_proto == ENA_ETH_IO_L3_PROTO_IPV4) {
			result |= kChecksumIP;

			if (!rxCtx.l3_csum_err)
				valid |= kChecksumIP;
			else
				ENA_DEBUG_LOG("Rx IP checksum error\n");
		}
	}

	// L4
	if (rxCtx.l4_csum_checked) {
		if (rxCtx.l4_proto == ENA_ETH_IO_L4_PROTO_TCP) {
			result |= kChecksumTCP;
			if (!rxCtx.l4_csum_err) {
				valid |= kChecksumTCP;
			} else {
				ENA_DEBUG_LOG("Rx TCP checksum error\n");
			}
		} else if (rxCtx.l4_proto == ENA_ETH_IO_L4_PROTO_UDP) {
			result |= kChecksumUDP;
			if (!rxCtx.l4_csum_err) {
				valid |= kChecksumUDP;
			} else {
				ENA_DEBUG_LOG("Rx TCP checksum error\n");
			}
		}
	}

	ENA_DEBUG_LOG("Rx checksum result: 0x%x, valid: 0x%x\n", result, valid);

	setChecksumResult(m, kChecksumFamilyTCPIP, result, valid);
}

bool ENA::enableAllIOQueues()
{
	ENAQueue *queue;
	ENARingTx *txRing;

	for (int i = 0; i < _numQueues; ++i) {
		queue = OSDynamicCast(ENAQueue, _queues->getObject(i));
		if (!queue->enable()) {
			ENA_ERROR_LOG("Failed to enable IO queue %d\n", i);
			disableAllIOQueues();
			return false;
		}
		txRing = queue->getTxRing();
		txRing->setupMissingTxCompletion(_missingTxCompletionTimeout,
						 _missingTxCompletionThreshold);
	}

	return true;
}

void ENA::disableAllIOQueues()
{
	ENAQueue *queue;

	for (int i = 0; i < _numQueues; ++i) {
		queue = OSDynamicCast(ENAQueue, _queues->getObject(i));
		queue->disable();
	}
}

void ENA::unmaskAllIOInterrupts() {
	ENAQueue *queue;

	// Unmask interrupts for all of the IO queues
	for(int i = 0; i < _numQueues; i++) {
		queue = OSDynamicCast(ENAQueue, _queues->getObject(i));
		queue->unmaskIOInterrupt();
	}
}

void ENA::checkForMissingKeepAlive()
{
	if (!testFlag(ENA_FLAG_WATCHDOG_ACTIVE))
		return;

	if (unlikely(_lastKeepAliveTime + _keepAliveTimeout <= ENATime::getNow())) {
		ENA_ERROR_LOG("Keep alive watchdog timeout\n");

		incAtomicStat(&_hwStats.watchdogExpired);
		triggerReset(ENA_REGS_RESET_KEEP_ALIVE_TO);
	}
}

void ENA::checkForAdminComState()
{
	if (unlikely(!ena_com_get_admin_running_state(_enaDev))) {
		ENA_ERROR_LOG("ENA admin queue is not in running state!\n");

		incAtomicStat(&_hwStats.adminQueuePause);
		triggerReset(ENA_REGS_RESET_ADMIN_TO);
	}
}

void ENA::checkForMissingCompletions()
{
	int budget, i;

	// Make sure the driver doesn't turn the device in other process
	atomic_thread_fence(memory_order_acquire);

	if (!testFlag(ENA_FLAG_DEV_UP))
		return;

	if (testFlag(ENA_FLAG_TRIGGER_RESET))
		return;

	if (_missingTxCompletionTimeout == ENA_HW_HINTS_NO_TIMEOUT)
		return;

	budget = min(_numQueues, ENA_MONITORED_QUEUES);

	for (i = 0; i < budget; ++i) {
		ENAQueue *queue = OSDynamicCast(
			ENAQueue,
			_queues->getObject(_lastMonitoredQueueID));
		ENARingTx *txRing = queue->getTxRing();
		ENARingRx *rxRing = queue->getRxRing();

		if (unlikely(!txRing->checkForMissingCompletions()))
			return;

		if (unlikely(!rxRing->checkForInterrupt()))
			return;

		++_lastMonitoredQueueID;
		_lastMonitoredQueueID %= _numQueues;
	}
}

void ENA::timerService(IOTimerEventSource *timerEvent)
{
	ENA_DEBUG_LOG("Timer service executed\n");

	updateStats();

	checkForMissingKeepAlive();

	checkForAdminComState();

	checkForMissingCompletions();

	if (unlikely(testFlag(ENA_FLAG_TRIGGER_RESET))) {
		ENA_ERROR_LOG("Trigger reset is on with reason: %d\n", _resetReason);
		_resetTask->callTask();
	}

	timerEvent->setTimeoutMS(ENA_TIMER_SERVICE_TIMEOUT_MS);
}

void ENA::aenqTimerService(IOTimerEventSource *timerEvent)
{
	ENA_DEBUG_LOG("Admin timer service executed\n");

	if (likely(testFlag(ENA_FLAG_DEV_RUNNING)))
		ena_com_aenq_intr_handler(_enaDev, this);

	timerEvent->setTimeoutMS(ENA_AENQ_TIMER_SERVICE_TIMEOUT_MS);
}

void ENA::resetAction()
{
	ENA_DEBUG_LOG("Reset action\n");

	if (unlikely(!testFlag(ENA_FLAG_TRIGGER_RESET))) {
		ENA_ERROR_LOG("Device reset schedule while reset bit is off\n");
		return;
	}

	IOLockLock(_globalLock);
	destroyDevice(false);
	restoreDevice();
	IOLockUnlock(_globalLock);
}

void ENA::triggerReset(enum ena_regs_reset_reason_types resetReason)
{
	if (likely(!testFlag(ENA_FLAG_TRIGGER_RESET))) {
		setFlag(ENA_FLAG_TRIGGER_RESET);
		_resetReason = resetReason;
	}
}

void ENA::destroyDevice(bool graceful)
{
	bool devUp;

	if (!testFlag(ENA_FLAG_DEV_RUNNING))
		return;

	setLinkStatus(kIONetworkLinkValid);

	_timerEvent->cancelTimeout();
	_aenqTimerEvent->cancelTimeout();

	devUp = testFlag(ENA_FLAG_DEV_UP);
	if (devUp)
		setFlag(ENA_FLAG_DEV_UP_BEFORE_RESET);
	else
		clearFlag(ENA_FLAG_DEV_UP_BEFORE_RESET);

	if (!graceful)
		ena_com_set_admin_running_state(_enaDev, false);

	if (devUp)
		disableLocked(nullptr);

	// Stop the device from sending AENQ events (in case reset flag is set
	//  and device is up, ena_down() already reset the device.
	if (!(testFlag(ENA_FLAG_TRIGGER_RESET) && devUp))
		ena_com_dev_reset(_enaDev, _resetReason);

	freeMainInterrupt();

	ena_com_abort_admin_commands(_enaDev);
	ena_com_wait_for_abort_completion(_enaDev);
	ena_com_admin_destroy(_enaDev);
	ena_com_mmio_reg_read_request_destroy(_enaDev);

	_resetReason = ENA_REGS_RESET_NORMAL;

	clearFlag(ENA_FLAG_TRIGGER_RESET);
	clearFlag(ENA_FLAG_DEV_RUNNING);
}

bool ENA::restoreDevice()
{
	struct ena_com_dev_get_features_ctx features;
	bool wdState;

	if (!_pciNub->open(this)) {
		ENA_ERROR_LOG("Cannot open PCI device\n");
		goto error;
	}

	setFlag(ENA_FLAG_ONGOING_RESET);
	if (!initDevice(&features, wdState)) {
		ENA_ERROR_LOG("Cannot initialize device\n");
		goto error;
	}

	if (!validateDeviceParameters(features)) {
		ENA_ERROR_LOG("Validation of device parameters failed\n");
		goto err_device_destroy;
	}

	if (wdState)
		setFlag(ENA_FLAG_WATCHDOG_ACTIVE);

	if (!setupMainInterrupt()) {
		ENA_ERROR_LOG("Setup main interrupt failed\n");
		goto err_device_destroy;
	}

	if (!requestMainInterrupt()) {
		ENA_ERROR_LOG("Setting main interrupt failed\n");
		goto err_device_destroy;
	}

	_pciNub->close(this);

	// If the interface was up before the reset bring it up
	if (testFlag(ENA_FLAG_DEV_UP_BEFORE_RESET)) {
		IOReturn ret = enableLocked(nullptr);
		if (ret != kIOReturnSuccess) {
			ENA_ERROR_LOG("Failed to create I/O queues\n");
			goto err_disable_msix;
		}
	}

	setFlag(ENA_FLAG_DEV_RUNNING);

	clearFlag(ENA_FLAG_ONGOING_RESET);
	if (testFlag(ENA_FLAG_LINK_UP))
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, _mediumAuto);

	ena_com_admin_aenq_enable(_enaDev);

	_aenqTimerEvent->setTimeoutMS(ENA_AENQ_TIMER_SERVICE_TIMEOUT_MS);

	// Update keep alive just before enabling timer service to prevent
	// false keep alive timeout - it could be possible if reset would take
	// too much time because of OS instability, as the last keep alives are
	// being suspended after destroyDevice() is finished.
	_lastKeepAliveTime.setNow();
	_timerEvent->enable();
	_timerEvent->setTimeoutMS(ENA_TIMER_SERVICE_TIMEOUT_MS);

	ENA_LOG("Device reset completed successfully, Driver version: %s\n", OSKextGetCurrentVersionString());

	return true;

err_disable_msix:
	freeMainInterrupt();
err_device_destroy:
	ena_com_abort_admin_commands(_enaDev);
	ena_com_wait_for_abort_completion(_enaDev);
	ena_com_admin_destroy(_enaDev);
	ena_com_dev_reset(_enaDev, ENA_REGS_RESET_DRIVER_INVALID_STATE);
	ena_com_mmio_reg_read_request_destroy(_enaDev);
error:
	clearFlag(ENA_FLAG_DEV_RUNNING);
	clearFlag(ENA_FLAG_ONGOING_RESET);
	ENA_ERROR_LOG("Reset attempt failed. Can not reset the device\n");

	if (_pciNub->isOpen())
		_pciNub->close(this);

	return false;
}

bool ENA::validateDeviceParameters(const struct ena_com_dev_get_features_ctx &features)
{
	if (memcmp(features.dev_attr.mac_addr, _macAddr.bytes, kIOEthernetAddressSize) != 0) {
		ENA_ERROR_LOG("Error, mac address are different\n");
		return false;
	}

	if (features.dev_attr.max_mtu < _currentMTU) {
		ENA_ERROR_LOG("Error, device max mtu is smaller than interface MTU\n");
		return false;
	}

	return true;
}

void ENA::keepAlive()
{
	_lastKeepAliveTime.setNow();
	ENA_DEBUG_LOG("Keep alive message, saved time: %llu\n", _lastKeepAliveTime.getAbsoluteTime());
}

void ENA::setRxDrops(UInt64 rxDrops)
{
	// Needs explicit conversion, as the inputErrors field is 32bit wide
	_netStats->inputErrors = static_cast<UInt32>(_rxErrors + rxDrops);

	setAtomicStat(&_hwStats.rxDrops, rxDrops);
}

void ENA::incrementRxErrors()
{
	// TODO: make this atomic upon adding multiqueue support
	++_rxErrors;
}

void ENA::setLinkUp()
{
	setFlag(ENA_FLAG_LINK_UP);
	if (!testFlag(ENA_FLAG_ONGOING_RESET))
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, _mediumAuto);
}

void ENA::setLinkDown()
{
	clearFlag(ENA_FLAG_LINK_UP);
	setLinkStatus(kIONetworkLinkValid);
}

bool ENA::publishProperties()
{
	if (!enaProperties.isValid()) {
		ENA_ERROR_LOG("Cannot publish statistics, missing symbols\n");
		return false;
	}

	for (size_t i = 0; i < ENAHWStatsNum; ++i) {
		_hwStatsProperties[i] = OSNumber::withNumber(0ULL, 64);
		if (_hwStatsProperties[i] == nullptr) {
			ENA_ERROR_LOG("Cannot publish HW statistics, failed to allocate OSNumber\n");
			return false;
		}

		if (!setProperty(enaProperties.getHWStatsSymbol(i), _hwStatsProperties[i])) {
			ENA_ERROR_LOG("Cannot publish HW statistics, failed set property\n");
			return false;
		}

	}

	for (size_t i = 0; i < ENATxStatsNum; ++i) {
		_txStatsProperties[i] = getArrayWithNumbers(_numQueues);
		if (_txStatsProperties[i] == nullptr) {
			ENA_ERROR_LOG("Cannot publish Tx statistics, failed to allocate array\n");
			return false;
		}

		if (!setProperty(enaProperties.getTxStatsSymbol(i), _txStatsProperties[i])) {
			ENA_ERROR_LOG("Cannot publish Tx statistics, failed set property\n");
			return false;
		}
	}

	for (size_t i = 0; i < ENARxStatsNum; ++i) {
		_rxStatsProperties[i] = getArrayWithNumbers(_numQueues);
		if (_rxStatsProperties[i] == nullptr) {
			ENA_ERROR_LOG("Cannot publish Rx statistics, failed to allocate array\n");
			return false;
		}

		if (!setProperty(enaProperties.getRxStatsSymbol(i), _rxStatsProperties[i])) {
			ENA_ERROR_LOG("Cannot publish Rx statistics, failed set property\n");
			return false;
		}
	}

	if (!super::publishProperties()) {
		ENA_ERROR_LOG("Failed to publish properties of the base class\n");
		return false;
	}

	return true;
}

void ENA::releaseProperties()
{
	for (auto &property : _hwStatsProperties)
		SAFE_RELEASE(property);

	for (auto &property : _txStatsProperties)
		SAFE_RELEASE(property);

	for (auto &property : _rxStatsProperties)
		SAFE_RELEASE(property);
}

void ENA::updateStats()
{
	OSNumber **propertyNumber;
	ENAQueue *queue;
	const ENATxStats *txStats;
	const ENARxStats *rxStats;
	const AtomicStat *stats;
	UInt64 totalRxPackets = 0;
	UInt64 totalTxPackets = 0;
	UInt64 totalRxBytes = 0;
	UInt64 totalTxBytes = 0;

	// Sum up total Tx/Rx stats from all of the queues
	for (unsigned int n = 0; n < _numQueues; ++n) {
		queue = OSDynamicCast(ENAQueue, _queues->getObject(n));
		txStats = queue->getTxRing()->getTxStats();
		rxStats = queue->getRxRing()->getRxStats();

		totalRxPackets += getAtomicStat(&rxStats->count);
		totalTxPackets += getAtomicStat(&txStats->count);
		totalRxBytes += getAtomicStat(&rxStats->bytes);
		totalTxBytes += getAtomicStat(&txStats->bytes);
	}

	_netStats->inputPackets = (UInt32)totalRxPackets;
	_netStats->outputPackets = (UInt32)totalTxPackets;

	_hwStats.rxPackets = totalRxPackets;
	_hwStats.txPackets = totalTxPackets;
	_hwStats.rxBytes = totalRxBytes;
	_hwStats.txBytes = totalTxBytes;

	// HW stats
	stats = (const AtomicStat *)(&_hwStats);
	propertyNumber = _hwStatsProperties;
	for (size_t i = 0; i < ENAHWStatsNum; ++i)
		propertyNumber[i]->setValue(getAtomicStat(&stats[i]));

	// IO stats
	for (unsigned int n = 0; n < _numQueues; ++n) {
		queue = OSDynamicCast(ENAQueue, _queues->getObject(n));

		// Tx
		stats = (const AtomicStat *)queue->getTxRing()->getTxStats();
		copyStatsFromQueueToProperties(stats, ENATxStatsNum, _txStatsProperties, n);

		// Rx
		stats = (const AtomicStat *)queue->getRxRing()->getRxStats();
		copyStatsFromQueueToProperties(stats, ENARxStatsNum, _rxStatsProperties, n);
	}
}

ENAHWStats * ENA::getHWStats()
{
	return &_hwStats;
}

ENAParallelOutputQueue *ENA::createParallelOutputQueue()
{
	ENAParallelOutputQueue *outputQueue;
	OSArray *txRings;

	// Parallel output queue needs array with Tx rings which will be
	// used for transmitting.
	txRings = OSArray::withCapacity(_queues->getCount());
	if (txRings == nullptr) {
		ENA_DEBUG_LOG("Cannot allocate array for the Tx rings\n");
		return nullptr;
	}

	for (int i = 0; i < _queues->getCount(); ++i) {
		ENAQueue *queue = OSDynamicCast(ENAQueue, _queues->getObject(i));
		// Adding object to array shouldn't fail, as it was allocated
		// with enough capacity.
		txRings->setObject(queue->getTxRing());
	}

	// Create parallel output queue that allows enqueueing mbufs from multiple
	// threads, manages the flow and calls appropriate xmit function of the ring
	outputQueue = ENAParallelOutputQueue::withTarget(
		txRings,
		(IOOutputAction) &ENARingTx::sendPacket,
		_hashFunction,
		_queueSize);
	// Array with Tx rings is no longer needed
	txRings->release();

	return outputQueue;
}

bool ENA::rssInitDefault()
{
	int rc;

	rc = ena_com_rss_init(_enaDev, ENA_RX_RSS_TABLE_LOG_SIZE);
	if (unlikely(rc)) {
		ENA_ERROR_LOG("Cannot init indirection table\n");
		return false;
	}

	for (int i = 0; i < ENA_RX_RSS_TABLE_SIZE; i++) {
		UInt16 val = i % _numQueues;
		rc = ena_com_indirect_table_fill_entry(_enaDev, i, ENA_IO_RXQ_IDX(val));
		if (unlikely(rc != 0)) {
			ENA_ERROR_LOG("Cannot fill indirect table\n");
			ena_com_rss_destroy(_enaDev);
			return false;
		}
	}

	rc = ena_com_fill_hash_function(_enaDev, ENA_ADMIN_CRC32, NULL, ENA_HASH_KEY_SIZE, 0xFFFFFFFF);
	if (unlikely((rc != 0) && (rc != ENA_COM_UNSUPPORTED))) {
		ENA_ERROR_LOG("Cannot fill hash function\n");
		ena_com_rss_destroy(_enaDev);
		return false;
	}

	rc = ena_com_set_default_hash_ctrl(_enaDev);
	if (unlikely((rc != 0) && (rc != ENA_COM_UNSUPPORTED))) {
		ENA_ERROR_LOG("Cannot fill hash control\n");
		ena_com_rss_destroy(_enaDev);
		return false;
	}

	return true;
}

bool ENA::rssConfigure()
{
	int rc;

	// Set indirection table
	rc = ena_com_indirect_table_set(_enaDev);
	if (unlikely((rc != 0) && (rc != ENA_COM_UNSUPPORTED)))
		return false;

	// Configure hash function (if supported)
	rc = ena_com_set_hash_function(_enaDev);
	if (unlikely((rc != 0) && (rc != ENA_COM_UNSUPPORTED)))
		return false;

	// Configure hash inputs (if supported)
	rc = ena_com_set_hash_ctrl(_enaDev);
	if (unlikely((rc != 0) && (rc != ENA_COM_UNSUPPORTED)))
		return false;

	return true;
}

void ENA::lockInputQueue()
{
	IOLockLock(_inputQueueLock);
}

void ENA::unlockInputQueue()
{
	IOLockUnlock(_inputQueueLock);
}

void ENA::handleInterruptCompleted(ENAQueue* queue)
{
	// Check whenever all other interrupt handlers have been finished
	if (atomic_fetch_sub(&_interruptHandlersRemaining, 1) == 1) {
		queue->unmaskIOInterrupt(ENA_RX_IRQ_INTERVAL, ENA_TX_IRQ_INTERVAL);
	}
}

const char * ENA::getName(const IORegistryPlane * plane) const
{
	if (likely(plane == nullptr && _devName[0] != '\0'))
		return _devName;
	else
		return super::getName(plane);
}

void * ena_mem_alloc_coherent(size_t length,
			      IOPhysicalAddress *physAddr,
			      ena_mem_handle_t *memHandle)
{
	IOBufferMemoryDescriptor *mem;

	mem = IOBufferMemoryDescriptor::withOptions(
		kIOMemoryPhysicallyContiguous,
		length,
		PAGE_SIZE);
	if (mem == nullptr)
		return nullptr;

	if (mem->prepare() != kIOReturnSuccess) {
		mem->release();
		return nullptr;
	}

	*memHandle = mem;
	*physAddr = mem->getPhysicalAddress();

	return mem->getBytesNoCopy();
}

void ena_mem_free_coherent(ena_mem_handle_t memHandle)
{
	IOBufferMemoryDescriptor *mem = (IOBufferMemoryDescriptor *)memHandle;
	mem->complete();
	mem->release();
}

/******************************************************************************
 ******************************** AENQ Handlers *******************************
 *****************************************************************************/

static void ena_update_on_link_change(void *controller_data,
				      struct ena_admin_aenq_entry *aenq_e)
{
	struct ENA *controller = (ENA *)controller_data;
	struct ena_admin_aenq_link_change_desc *desc;
	int status;

	desc = (struct ena_admin_aenq_link_change_desc *)aenq_e;
	status = desc->flags & ENA_ADMIN_AENQ_LINK_CHANGE_DESC_LINK_STATUS_MASK;

	if (status) {
		ENA_DEBUG_LOG_EXT(controller, "%s - Link up\n", __func__);
		controller->setLinkUp();
	} else {
		ENA_DEBUG_LOG_EXT(controller, "%s - Link down\n", __func__);
		controller->setLinkDown();
	}
}

static void ena_keep_alive_wd(void *controller_data,
			      struct ena_admin_aenq_entry *aenq_e)
{
	ENA *controller = (ENA *)controller_data;
	struct ena_admin_aenq_keep_alive_desc *desc;
	UInt64 rxDrops;

	desc = (struct ena_admin_aenq_keep_alive_desc *)aenq_e;
	controller->keepAlive();

	rxDrops = ((u64)desc->rx_drops_high << 32) | desc->rx_drops_low;

	controller->setRxDrops(rxDrops);
}

// This handler will called for unknown event group or unimplemented handlers
static void unimplemented_aenq_handler(void *data,
				       struct ena_admin_aenq_entry *aenq_e)
{
	ENA_ERROR_LOGC("Unknown event was received or event with unimplemented handler\n");
}

struct ena_aenq_handlers ENA::aenqHandlers = {
	.handlers = {
		[ENA_ADMIN_LINK_CHANGE] = ena_update_on_link_change,
		[ENA_ADMIN_NOTIFICATION] = unimplemented_aenq_handler,
		[ENA_ADMIN_KEEP_ALIVE] = ena_keep_alive_wd,
	},
	.unimplemented_handler = unimplemented_aenq_handler
};
