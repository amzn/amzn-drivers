// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENAHash.hpp"

#include <IOKit/IOLib.h>

#define super OSObject
OSDefineMetaClassAndAbstractStructors(ENAHash, OSObject)

bool ENAHash::init(const UInt8 *key, size_t keyLen)
{
	if (!super::init())
		return false;

	_key = IONew(UInt8, keyLen);
	if (_key == nullptr)
		return false;

	memcpy(_key, key, keyLen);
	_keyLen = keyLen;

	return true;
}

UInt32 ENAHash::getHash(struct in_addr sourceAddr, struct in_addr destAddr)
{
	UInt8 data[sizeof(sourceAddr) + sizeof(destAddr)];
	size_t dataSize;

	dataSize = 0;
	memcpy(&data[dataSize], &sourceAddr, sizeof(sourceAddr));
	dataSize += sizeof(sourceAddr);
	memcpy(&data[dataSize], &destAddr, sizeof(destAddr));
	dataSize += sizeof(destAddr);

	return getHash(data, dataSize);
}

UInt32 ENAHash::getHash(struct in_addr sourceAddr,
		 struct in_addr destAddr,
		 unsigned short sourcePort,
		 unsigned short destPort)
{
	UInt8 data[sizeof(sourceAddr) + sizeof(destAddr) + sizeof(sourcePort) + sizeof(destPort)];
	size_t dataSize;

	dataSize = 0;
	memcpy(&data[dataSize], &sourceAddr, sizeof(sourceAddr));
	dataSize += sizeof(sourceAddr);
	memcpy(&data[dataSize], &destAddr, sizeof(destAddr));
	dataSize += sizeof(destAddr);
	memcpy(&data[dataSize], &sourcePort, sizeof(sourcePort));
	dataSize += sizeof(sourcePort);
	memcpy(&data[dataSize], &destPort, sizeof(destPort));
	dataSize += sizeof(destPort);

	return getHash(data, dataSize);
}

void ENAHash::free()
{
	if (_key != nullptr) {
		IODelete(_key, UInt8, _keyLen);
		_key = nullptr;
	}

	super::free();
}
