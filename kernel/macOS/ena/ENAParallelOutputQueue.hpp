// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENAPARALLELOUTPUTQUEUE_HPP_
#define _ENAPARALLELOUTPUTQUEUE_HPP_

#include "ENAHash.hpp"

#include <IOKit/network/IOBasicOutputQueue.h>

/*! @class ENAParallelOutputQueue
    @abstract A packets queue that supports multiple producers and multiple
    consumers in multithreading safe mather.
    @discussion The class supports multiple consumers support by wrapping
    multiple IOBasicOutputQueue objects. Whenever the producer enqueues a packet,
    the mbuf is assigned to the IOBasicOutputQueue using calucalted HASH.
*/
class ENAParallelOutputQueue : public IOOutputQueue
{
	OSDeclareDefaultStructors(ENAParallelOutputQueue)
public:

/*!	@function withTarget
	@abstract Factory method that constructs and initializes an
	ENAParallelOutputQueue object.
	@param targets The objects that will handle packets removed from the queues.
	@param action The pointer to the method of the objects, that should handle
	the packets removed from the queues.
	@param hash The pointer to the hashing function, which is going to be
	used for flow distribution.
	@param capacity The initial capacity of each output queues.
	@param priorities The number of traffic priorities supported.
	@result Returns an ENAParallelOutputQueue object on success, or 0 otherwise.
*/
	static ENAParallelOutputQueue * withTarget(OSArray *targets,
						   IOOutputAction action,
						   ENAHash *hash,
                				   UInt32 capacity = 0,
						   UInt32 priorities = 1);

/*!	@function init
	@abstract Initializes an ENAParallelOutputQueue object.
	@param targets The objects that will handle packets removed from the queues.
	@param action The pointer to the method of the objects, that should handle
	the packets removed from the queues.
	@param hash The pointer to the hashing function, which is going to be
	used for flow distribution.
	@param capacity The initial capacity of each output queues.
	@param priorities The number of traffic priorities supported.
	@result Returns true if initialized successfully, false otherwise.
*/
	bool init(OSArray *targets,
			  IOOutputAction action,
			  ENAHash *hash,
                	  UInt32 capacity = 0,
			  UInt32 priorities = 1);

/*!	@function start
	@abstract Starts the packet flow between the queue and its targets.
	@discussion Called by the target to start the queue. This will allow
	packets to be removed from the queue, and then deliver them to the target.
	This method starts all allocated sub-queues.
	@result Returns true if the queue was started successfully, false otherwise.
*/
	bool start() APPLE_KEXT_OVERRIDE;

/*!	@function stop
	@abstract Stops the packet flow between the queue and its target.
	@discussion This method stops the queue and prevents it from sending
	packets to its target. This call is synchronous and it may block. After
	this method returns, the queue will no longer call the registered
	targets actions, even as new packets are added to the queue. The queue
	will continue to absorb new packets until the size of the queue reaches
	its capacity. The registered action must never call stop(), or a deadlock
	will occur.
	This method stops all allocated sub-queues.
	@result Returns the previous running state of the queues, true if one of
	the the queues was running, false if all the queues were already stopped.
*/
	bool stop() APPLE_KEXT_OVERRIDE;

/*!	@function service
	@abstract Services queues that were stalled by the targets.
	@discussion This method calls service() for each active sub-queues,
	which indicates that all of them are ready to accept new packets.
	@param options Options for the service request.
	@result Returns true if one the queue was stalled and there were packets
	sitting in the queue awaiting for delivery, false otherwise.
*/
	bool service(IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;

/*!	@function service
	@abstract Services a queue that was stalled by the target.
	@discussion A target that stalls the queue must call service() when
	it becomes ready to accept more packets. Calling this method when the
	queue is not stalled is harmless.
	@param index Index of the sub-queue that should be serviced.
	@param options Options for the service request.
	@result Returns true if the queue was stalled and there were packets
	sitting in the queue awaiting for delivery, false otherwise.
*/
	bool service(int index, IOOptionBits options = 0);

/*!	@function enqueue
	@abstract Adds a packet, or a chain of packets, to the sub-queue.
	@discussion This method is called by a client to add a packet, or a
	chain of packets, to the queue. A packet is described by an mbuf chain,
	while a chain of packets is constructed by linking multiple mbuf chains
	via the m_nextpkt field. This method can be called by multiple client
	threads. Sub-queue is being choosen basing on hash calculated from:
	  * Source IP + Destination IP for IPv4 packets
	  * Source IP + Destination IP + Source port + Destination port
	    for IPv4/(TCP|UDP) packets
	@param mbuf A single packet, or a chain of packets.
	@param param A parameter provided by the caller.
	@result Always returns 0.
*/
	UInt32 enqueue(mbuf_t mbuf, void * param) APPLE_KEXT_OVERRIDE;

/*!	@function flush
	@abstract Drops and frees all packets which are currently held by all the sub-queues.
	@discussion To ensure that all packets are removed from the queue,
	stop() should be called prior to flush(), to make sure there are
	no packets in-flight and being delivered to the target.
	@result Returns the number of packets that were dropped and freed.
*/
	UInt32 flush() APPLE_KEXT_OVERRIDE;

/*!	@function setCapacity
	@abstract Changes the number of packets that each sub-queue can hold
	before it begins to drop the excessed packets.
	@param capacity The new desired capacity.
	@result Returns true if the new capacity was accepted, false otherwise.
*/
	bool setCapacity(UInt32 capacity) APPLE_KEXT_OVERRIDE;

/*!	@function getCapacity
	@abstract Gets the number of packets that each sub-queue can hold.
	@discussion The sub-queue will begin to drop incoming packets when the
	size of the sub-queue reaches its capacity.
	@result Returns the current each sub-queue capacity.
*/
	UInt32 getCapacity() const APPLE_KEXT_OVERRIDE;

/*!	@function getSize
	@abstract Gets the number of packets which are currently held in all sub-queues.
	@result Returns the size of all sub-queues.
*/
	UInt32 getSize() const APPLE_KEXT_OVERRIDE;

/*!	@function getSize
	@abstract Gets the number of packets which are currently held in the given sub-queue.
	@param index Index of the sub-queue.
	@result Returns the size of the given sub-queue.
*/
	UInt32 getSize(int index) const;

/*!	@function setActiveSubQueues
	@abstract Set number of active sub-queues.
	@discussion Only sub-queues which are active will have allocated mbuf's
	queues to preserve memory. The mbuf's are being assigned only to active queues.
	@param queues Number of sub-queues to activate
	@results Returns true if is activated successfully, false otherwise.
*/
	bool setActiveSubQueues(UInt8 queues);

private:
	OSArray		*_outputQueues;
	OSArray		*_targets;
	IOLock		*_queueLock;
	IOOutputAction  _action;
	ENAHash		*_hash;
	UInt8		_targetsCount;
	UInt32		_priorities;
	UInt32		_capacity;
	UInt8		_active;

	void free() APPLE_KEXT_OVERRIDE;
};

#endif
