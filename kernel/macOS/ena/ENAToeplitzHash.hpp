// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENATOEPLITZHASH_HPP__
#define _ENATOEPLITZHASH_HPP__

#include "ENAHash.hpp"

class ENAToeplitzHash : public ENAHash
{
	OSDeclareDefaultStructors(ENAToeplitzHash);
public:

/*!	@function withKey
	@abstract Factory method that constructs and initializes an
	ENAToeplitzHash object.
	@param key Key for hash function.
	@param keyLen Length of the key in bytes.
	@result Returns an ENAToeplitzHash object on success, or 0 otherwise.
*/
	static ENAToeplitzHash * withKey(const UInt8 *key, size_t keyLen);

/*!	@function getHash
	@abstract Calculates hash for given data using Toeplitz algorithm.
	@param data Array of bytes, for which the hash has to be calculated.
	@param dataSize Size of the data in bytes.
	@result Returns hash calculated using Toeplitz algorithm.
*/
	UInt32 getHash(const UInt8 *data, size_t dataSize) APPLE_KEXT_OVERRIDE;
};

#endif
