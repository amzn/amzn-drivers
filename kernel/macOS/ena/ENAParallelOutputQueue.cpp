// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENAParallelOutputQueue.hpp"

#include <net/ethernet.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#define super IOOutputQueue
OSDefineMetaClassAndStructors(ENAParallelOutputQueue, IOOutputQueue)

#define QUEUE_LOCK	IOLockLock(_queueLock)
#define QUEUE_UNLOCK	IOLockUnlock(_queueLock)

ENAParallelOutputQueue * ENAParallelOutputQueue::withTarget(
		OSArray *targets,
		IOOutputAction action,
		ENAHash *hash,
		UInt32 capacity,
		UInt32 priorities)
{
	ENAParallelOutputQueue* queue = new ENAParallelOutputQueue;

	if (queue != nullptr && !queue->init(targets, action, hash, capacity, priorities)) {
		queue->release();
		queue = nullptr;
	}

	return queue;
}

bool ENAParallelOutputQueue::init(OSArray *targets,
				  IOOutputAction action,
				  ENAHash *hash,
				  UInt32 capacity,
				  UInt32 priorities)
{
	if (!super::init())
		return false;

	if ((targets == nullptr) || (targets->getCount() == 0) || (action == nullptr) ||
	    (hash == nullptr) || (priorities == 0) || (priorities > 256))
		return false;

	_targets = targets;
	_targetsCount = targets->getCount();
	_action = action;
	_hash = hash;
	_capacity = capacity;
	_priorities = priorities;

	_targets->retain();
	_hash->retain();

	_queueLock = IOLockAlloc();
	if (_queueLock == nullptr)
		return false;

	_outputQueues = OSArray::withCapacity(_targetsCount);
	if (_outputQueues == nullptr)
		return false;

	for (int i = 0; i < _targetsCount; ++i) {
		OSObject *target = _targets->getObject(i);

		IOBasicOutputQueue *outputQueue = IOBasicOutputQueue::withTarget(
				target,
				_action,
				_capacity,
				_priorities);
		if (outputQueue == nullptr)
			return false;

		// It cannot fail, as there is already enough capacity reserved.
		_outputQueues->setObject(outputQueue);
		// The object is being retained by OSArray, so it can be released.
		outputQueue->release();
	}

	return true;
}

void ENAParallelOutputQueue::free()
{
	cancelServiceThread();

	if (_queueLock != nullptr) {
		flush();
		IOLockFree(_queueLock);
		_queueLock = nullptr;
	}

	if (_outputQueues != nullptr) {
		_outputQueues->release();
		_outputQueues = nullptr;
	}

	if (_hash != nullptr) {
		_hash->release();
		_hash = nullptr;
	}

	if (_targets != nullptr) {
		_targets->release();
		_targets = nullptr;
	}

	super::free();
}

constexpr int etherVLANTagLen = 4;
constexpr int etherVLANHeaderLen = ETHER_HDR_LEN + etherVLANTagLen;
constexpr int etherTypeVLANOffset = ETHER_HDR_LEN + 2;

UInt32 ENAParallelOutputQueue::enqueue(mbuf_t mbuf, void * param)
{
	struct ether_header *etherHeader;
	struct ip *ip;
	struct tcphdr *tcpHeader;
	struct udphdr *udpHeader;
	struct in_addr sourceAddr;
	struct in_addr destAddr;
	IOBasicOutputQueue *outputQueue;
	mbuf_t m;
	size_t mlen;
	int etherHeaderLen;
	int ipHeaderLen = 0;
	int targetQueue;
	UInt32 ret;
	UInt32 hash;
	unsigned short sourcePort;
	unsigned short destPort;
	UInt16 etherType;
	UInt8 *headerPointer;
	u_char ipProto = 0;

	assert(mbuf_flags(mbuf) & MBUF_PKTHDR);
	assert(_active > 0);

	m = mbuf;
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

	if (etherType == ETHERTYPE_IP) {
		ip = reinterpret_cast<struct ip *>(headerPointer);
		ipHeaderLen = ip->ip_hl << 2;
		sourceAddr = ip->ip_src;
		destAddr = ip->ip_dst;
		ipProto = ip->ip_p;

		// Advance the header pointer for L4 header
		if (ipHeaderLen == mlen) {
			m = mbuf_next(m);
			headerPointer = reinterpret_cast<UInt8 *>(mbuf_data(m));
		} else {
			headerPointer += ipHeaderLen;
		}

		if (ipProto == IPPROTO_TCP) {
			tcpHeader = reinterpret_cast<struct tcphdr *>(headerPointer);
			sourcePort = tcpHeader->th_sport;
			destPort = tcpHeader->th_dport;
			hash = _hash->getHash(sourceAddr, destAddr, sourcePort, destPort);
		} else if (ipProto == IPPROTO_UDP) {
			udpHeader = reinterpret_cast<struct udphdr *>(headerPointer);
			sourcePort = udpHeader->uh_sport;
			destPort = udpHeader->uh_dport;
			hash = _hash->getHash(sourceAddr, destAddr, sourcePort, destPort);
		} else {
			hash = _hash->getHash(sourceAddr, destAddr);
		}

	} else {
		// As for now, hash calculation for IPv6 is not supported, yet.
		hash = 0;
	}

	// Select appropriate queue using hash value.
	targetQueue = hash % _active;

	outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(targetQueue));
	assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", targetQueue);
	ret = outputQueue->enqueue(mbuf, param);

	return ret;
}

bool ENAParallelOutputQueue::start()
{
	bool success = true;

	QUEUE_LOCK;
	for (int i = 0; i < _targetsCount; ++i) {
		IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(i));
		assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", i);
		success &= outputQueue->start();
	}
	QUEUE_UNLOCK;

	return success;
}

bool ENAParallelOutputQueue::stop()
{
	bool success = true;

	QUEUE_LOCK;
	for (int i = 0; i < _targetsCount; ++i) {
		IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(i));
		assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", i);
		success &= outputQueue->stop();
	}
	QUEUE_UNLOCK;

	return true;
}

bool ENAParallelOutputQueue::service(IOOptionBits options)
{
	bool success = true;

	QUEUE_LOCK;
	// Service only active queues
	for (int i = 0; i < _active; ++i) {
		IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(i));
		assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", i);
		success &= outputQueue->service(options);
	}
	QUEUE_UNLOCK;

	return success;
}

bool ENAParallelOutputQueue::service(int index, IOOptionBits options)
{
	IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(index));
	assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", index);
	return outputQueue->service(options);
}

UInt32 ENAParallelOutputQueue::flush()
{
	UInt32 flushCount = 0;

	QUEUE_LOCK;
	for (int i = 0; i < _targetsCount; ++i) {
		IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(i));
		assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", i);
		flushCount += outputQueue->flush();
	}
	QUEUE_UNLOCK;

	return flushCount;
}

bool ENAParallelOutputQueue::setCapacity(UInt32 capacity)
{
	bool success = true;

	QUEUE_LOCK;
	_capacity = capacity;
	for (int i = 0; i < _active; ++i) {
		IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(i));
		assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", i);
		success &= outputQueue->setCapacity(_capacity);
	}
	QUEUE_UNLOCK;

	return success;
}

UInt32 ENAParallelOutputQueue::getSize() const
{
	UInt32 size = 0;

	for (int i = 0; i < _active; ++i)
		size += getSize(i);

	return size;
}

UInt32 ENAParallelOutputQueue::getSize(int index) const
{
	IOBasicOutputQueue *outputQueue = OSDynamicCast(IOBasicOutputQueue, _outputQueues->getObject(index));
	assertf(outputQueue != nullptr, "IOBasicOutputQueue cannot be null: queue %u\n", index);
	return outputQueue->getSize();
}

bool ENAParallelOutputQueue::setActiveSubQueues(UInt8 queues)
{
	if (queues == 0 || queues > _targetsCount)
		return false;

	_active = queues;

	return setCapacity(_capacity);
}
