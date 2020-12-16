// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENANaiveHash.hpp"

#define super ENAHash
OSDefineMetaClassAndStructors(ENANaiveHash, ENAHash)

ENANaiveHash * ENANaiveHash::withKey(const UInt8 *key, size_t keyLen)
{
	ENANaiveHash *naiveHash = new ENANaiveHash;

	if (naiveHash != nullptr && !naiveHash->init(key, keyLen))
	{
        	naiveHash->release();
		naiveHash = nullptr;
	}

	return naiveHash;
}

UInt32 ENANaiveHash::getHash(const UInt8 *data, size_t dataSize)
{
	UInt32 hash = 0;
	size_t i;

	for (i = 0; i < _keyLen; i++)
		hash += _key[i];
	for (i = 0; i < dataSize; i++)
		hash += data[i];

	return hash;
}
