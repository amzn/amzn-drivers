// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENA_HPP
#define _ENA_HPP

#include "ENAParallelOutputQueue.hpp"
#include "ENARings.hpp"
#include "ENATask.hpp"
#include "ENATime.hpp"
#include "ENAToeplitzHash.hpp"
#include "ENAUtils.hpp"

#include "ena_com/ena_com.h"

#include <net/ethernet.h>

#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>

#include <IOKit/network/IOEthernetInterface.h>

#include <IOKit/pci/IOPCIDevice.h>

#define ENA_IO_TXQ_IDX(q)               (2 * (q))
#define ENA_IO_RXQ_IDX(q)               (2 * (q) + 1)

#define DEFAULT_TX_QUEUE_SIZE		1024
#define ENA_DEFAULT_RING_SIZE		1024

#define ENA_RX_RSS_TABLE_LOG_SIZE	7
#define ENA_RX_RSS_TABLE_SIZE		(1 << ENA_RX_RSS_TABLE_LOG_SIZE)

#define ENA_HASH_KEY_SIZE		40

#define ENA_REG_BAR			kIOPCIConfigBaseAddress0
#define ENA_MEM_BAR			kIOPCIConfigBaseAddress2

#define ENA_MMIO_DISABLE_REG_READ	BIT(0)

#define ENA_RX_REFILL_THRESH_DIVIDER	8

#define ENA_CLEANUP_BUDGET		8

#define ENA_TX_BUDGET			128
#define ENA_RX_BUDGET			256

#define ENA_TX_COMMIT			32

#define ENA_RX_IRQ_INTERVAL		20
#define ENA_TX_IRQ_INTERVAL		50

#define ENA_DEVICE_KALIVE_TIMEOUT	6
#define ENA_TX_TIMEOUT			5
#define ENA_MAX_NUM_OF_TIMEOUTED_PACKETS 128
#define ENA_MONITORED_QUEUES		4
#define ENA_MAX_NO_INTERRUPT_ITERATIONS	3

#define ENA_TIMER_SERVICE_TIMEOUT_MS		1000
#define ENA_AENQ_TIMER_SERVICE_TIMEOUT_MS	500

#define PCI_DEV_ID_ENA_PF		0x0ec2
#define PCI_DEV_ID_ENA_VF		0xec20

#define ENA_MAIN_IRQ_PF_IDX		1
#define ENA_MAIN_IRQ_VF_IDX		0

#define ENA_MAX_NAME_LEN		32

#define ENA_SUPPORTED_OFFLOADS (kChecksumIP | kChecksumTCP | kChecksumTCPNoPseudoHeader)

// Used key provided Microsoft:
// https://docs.microsoft.com/en-us/windows-hardware/drivers/network/verifying-the-rss-hash-calculation
constexpr UInt8 hashFunctionKey[] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa,
};

enum ENAFlags
{
	ENA_FLAG_DEV_UP,
	ENA_FLAG_DEV_INIT,
	ENA_FLAG_DEV_RUNNING,
	ENA_FLAG_TRIGGER_RESET,
	ENA_FLAG_ONGOING_RESET,
	ENA_FLAG_DEV_UP_BEFORE_RESET,
	ENA_FLAG_WATCHDOG_ACTIVE,
	ENA_FLAG_LINK_UP,
};

// Encapsulates interrupt configuration
struct ENAIrq
{
	int 				source;		// Number of the interrupt source
	IOFilterInterruptAction		handler;	// Handler executed when interrupt comes
	IOFilterInterruptEventSource	*interruptSrc;	// Object descripting interrupt event
	IOWorkLoop			*workLoop;	// Used for interrupt synchronisation
};

struct ENAHWStats
{
	AtomicStat rxPackets;
	AtomicStat txPackets;
	AtomicStat rxBytes;
	AtomicStat txBytes;
	AtomicStat rxDrops;
	AtomicStat adminQueuePause;
	AtomicStat watchdogExpired;
};
constexpr size_t ENAHWStatsNum = sizeof(ENAHWStats)/sizeof(AtomicStat);

class ENA : public IOEthernetController
{
	OSDeclareDefaultStructors(ENA)
public:
	bool init(OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;

	// Configure the device to a working state
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;

	// Interface up/down
	IOReturn enable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;
	IOReturn disable(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;

	IOReturn getHardwareAddress(IOEthernetAddress *addrP) APPLE_KEXT_OVERRIDE;

	// Override work loop getters/setters for not using global work loop
	bool createWorkLoop() APPLE_KEXT_OVERRIDE;
	IOWorkLoop *getWorkLoop() const APPLE_KEXT_OVERRIDE;

	// Return IOOutputQueue created by the ENA driver - override default
	// implementation as ENA uses output queue which has dependency on
	// driver resources
	IOOutputQueue *getOutputQueue() const APPLE_KEXT_OVERRIDE;

	// Interface configuration
	IOReturn setMulticastMode(bool active) APPLE_KEXT_OVERRIDE;
	IOReturn setPromiscuousMode(bool active) APPLE_KEXT_OVERRIDE;

	IOReturn setMaxPacketSize(UInt32 maxSize) APPLE_KEXT_OVERRIDE;
	IOReturn getMaxPacketSize(UInt32 *maxSize) const APPLE_KEXT_OVERRIDE;

	IOReturn getChecksumSupport(UInt32 *checksumMask,
				    UInt32 checksumFamily,
				    bool isOutput) APPLE_KEXT_OVERRIDE;
	UInt32 getFeatures() const APPLE_KEXT_OVERRIDE;

	bool configureInterface(IONetworkInterface *netif) APPLE_KEXT_OVERRIDE;

	// Get the custom name if the plane is 0 (nullptr).
	// Format of the name is like: AmazonENAEthernet bb:dd.ff
	// Allows for unique identification of the multiple interfaces in the dmesg.
	const char * getName(const IORegistryPlane * plane = 0) const APPLE_KEXT_OVERRIDE;

	void setupTxChecksum(struct ena_com_tx_ctx &txCtx, mbuf_t m);
	void processRxChecksum(struct ena_com_rx_ctx &rxCtx, mbuf_t m);

	bool testFlag(ENAFlags flag);

	bool handleMainInterrupt(IOFilterInterruptEventSource *src);
	void handleInterruptCompleted(ENAQueue* queue);

	struct ena_com_dev * getENADevice();

	IOPCIDevice * getProvider();
	IOEthernetInterface * getEthernetInterface();
	IONetworkStats * getNetworkStats();

	void triggerReset(enum ena_regs_reset_reason_types resetReason);

	void keepAlive();
	void setRxDrops(UInt64 rxDrops);

	void incrementRxErrors();

	void setLinkUp();
	void setLinkDown();

	ENAHWStats * getHWStats();

	void lockInputQueue();
	void unlockInputQueue();

private:
	struct ena_com_dev 	*_enaDev;
	OSArray			*_queues;
	OSArray			*_txStatsProperties[ENATxStatsNum];
	OSArray			*_rxStatsProperties[ENARxStatsNum];
	OSNumber		*_hwStatsProperties[ENAHWStatsNum];
	IOLock			*_globalLock;
	IOLock			*_inputQueueLock;
	IOEthernetInterface	*_netif;
	IOPCIDevice		*_pciNub;
	IOWorkLoop		*_workLoop;
	IOWorkLoop		*_aenqWorkLoop;
	IOMemoryMap		*_regMap; // Register BAR0
	IOMemoryMap		*_memMap; // Memory BAR2
	IOEthernetStats 	*_etherStats;
	IONetworkStats		*_netStats;
	IONetworkMedium		*_mediumAuto; // For auto-neg medium
	IOTimerEventSource	*_timerEvent;
	IOTimerEventSource	*_aenqTimerEvent;
	ENAParallelOutputQueue	*_txQueue;
	ENATask			*_resetTask;
	ENAHash			*_hashFunction;
	enum ena_regs_reset_reason_types _resetReason;
	IOEthernetAddress	_macAddr;
	ENAHWStats		_hwStats;
	ENAIrq			_mainIrq;
	ENATime			_lastKeepAliveTime;
	ENATime			_keepAliveTimeout;
	ENATime			_missingTxCompletionTimeout;
	UInt64			_flags;
	UInt64			_rxErrors;
	UInt32			_queueSize;
	UInt32			_numQueues;
	UInt32			_maxMTU;
	UInt32			_currentMTU;
	UInt32			_txChecksumMask;
	UInt32			_rxChecksumMask;
	UInt32			_featuresMask;
	UInt32			_lastMonitoredQueueID;
	UInt32			_missingTxCompletionThreshold;
	UInt16			_maxTxSglSize;
	UInt16			_maxRxSglSize;
	atomic_int_fast8_t	_interruptHandlersRemaining;
	char			_devName[ENA_MAX_NAME_LEN];

	static struct ena_aenq_handlers aenqHandlers;
	constexpr static UInt8 etherVLANTagLen = 4;
	constexpr static UInt8 etherVLANHeaderLen = ETHER_HDR_LEN + etherVLANTagLen;
	constexpr static UInt8 etherTypeVLANOffset = ETHER_HDR_LEN + 2;

	// Maximum Tx buffer size is equal to max jumbo MTU (9001) + max ethernet header size
	constexpr static UInt32 txBufMaxSize = 9001 + etherVLANHeaderLen;

	// Optimal number of IO queues for the best performance results. OS limit.
	constexpr static UInt32 maxSupportedIOQueues = 1;

	int configHostInfo();

	bool initDevice(struct ena_com_dev_get_features_ctx *features, bool &wdState);
	bool initEventSources(IOService *provider);

	bool allocateIOQueues();

	void initPCIConfigSpace();
	bool allocatePCIResources();

	UInt32 calcIOQueueNum(const struct ena_com_dev_get_features_ctx &features);
	UInt32 calcIOQueueSize(const struct ena_com_dev_get_features_ctx &features,
			       UInt16 *txSglSize,
			       UInt16 *rxSglSize);
	void setSupportedOffloads(const struct ena_admin_feature_offload_desc &offloads);

	bool setupMainInterrupt();
	bool requestMainInterrupt();
	void freeMainInterrupt();

	void releasePCIResources();

	void setFlag(ENAFlags flag);
	void clearFlag(ENAFlags flag);

	bool enableAllIOQueues();
	void disableAllIOQueues();

	void unmaskAllIOInterrupts();

	void checkForMissingKeepAlive();
	void checkForAdminComState();
	void checkForMissingCompletions();

	void timerService(IOTimerEventSource *timerEvent);
	void aenqTimerService(IOTimerEventSource *timerEvent);

	void resetAction();

	void destroyDevice(bool graceful);
	bool restoreDevice();

	bool validateDeviceParameters(const struct ena_com_dev_get_features_ctx &features);

	IOReturn enableLocked(IONetworkInterface *netif);
	IOReturn disableLocked(IONetworkInterface *netif);

	bool publishProperties() APPLE_KEXT_OVERRIDE;
	void releaseProperties();

	void updateStats();

	ENAParallelOutputQueue *createParallelOutputQueue();

	bool rssInitDefault();
	bool rssConfigure();
};

#endif /* _ENA_HPP */
