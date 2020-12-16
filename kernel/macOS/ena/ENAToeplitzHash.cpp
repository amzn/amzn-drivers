// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENAToeplitzHash.hpp"

#define super ENAHash
OSDefineMetaClassAndStructors(ENAToeplitzHash, ENAHash)

ENAToeplitzHash * ENAToeplitzHash::withKey(const UInt8 *key, size_t keyLen)
{
	ENAToeplitzHash *toeplitzHash = new ENAToeplitzHash;

	if (toeplitzHash != nullptr && !toeplitzHash->init(key, keyLen))
	{
		toeplitzHash->release();
		toeplitzHash = nullptr;
	}

	return toeplitzHash;
}

UInt32 ENAToeplitzHash::getHash(const UInt8 *data, size_t dataSize)
{
	UInt32 keyWindow;
	UInt32 hash = 0;
	UInt32 i, b;

	keyWindow = (_key[0] << 24) + (_key[1] << 16) + (_key[2] << 8) + _key[3];

	// Iterate over data
	for (i = 0; i < dataSize; ++i) {
		// Iterate over byte
		for (b = 0; b < 8; ++b) {
			if (data[i] & (1 << (7 - b)))
				hash ^= keyWindow;
			keyWindow <<= 1;

			// Read next bit of the key if possible
			if ((i + 4) < _keyLen && (_key[i + 4] & (1 << (7 - b))))
				keyWindow |= 1;
		}
	}

	return hash;
}
