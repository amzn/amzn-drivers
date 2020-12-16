// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENAHASH_HPP__
#define _ENAHASH_HPP__

#include <netinet/ip.h>

#include <libkern/OSTypes.h>
#include <libkern/c++/OSObject.h>

class ENAHash : public OSObject
{
	OSDeclareDefaultStructors(ENAHash);
public:

/*!	@function init
	@abstract Initializes an ENAHash object.
	@param key Key for hash function.
	@param keyLen Length of the key in bytes.
	@result Returns true if initialized successfully, false otherwise.
*/
	virtual bool init(const UInt8 *key, size_t keyLen);

/*!	@function getHash
	@abstract Abstract function for calculating hash for given data.
	@param data Array of bytes, for which the hash has to be calculated.
	@param dataSize Size of the data in bytes.
	@result Returns hash for the data.
*/
	virtual UInt32 getHash(const UInt8 *data, size_t dataSize) = 0;

/*!	@function getHash
	@abstract Calculates hash for 2 tuple of IP packet.
	@param sourceAddr Source address from the IP header.
	@param destAddr Destination address from the IP header.
	@result Returns hash for the 2 tuple.
*/
	UInt32 getHash(struct in_addr sourceAddr, struct in_addr destAddr);

/*!	@function getHash
	@abstract Calculates hash for 4 tuple of IP/TCP IP/UDP packet.
	@param sourceAddr Source address from the IP header.
	@param destAddr Destination address from the IP header.
	@param sourcePort Source port from the TCP/UDP header.
	@param destPort Destination port from the TCP/UDP header.
	@result Returns hash for the 4 tuple.
*/
	UInt32 getHash(struct in_addr sourceAddr,
			       struct in_addr destAddr,
			       unsigned short sourcePort,
			       unsigned short destPort);

protected:
	UInt8 *_key;
	size_t _keyLen;

	virtual void free() APPLE_KEXT_OVERRIDE;
};

#endif
