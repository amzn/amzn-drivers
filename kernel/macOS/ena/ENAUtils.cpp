// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENAUtils.hpp"

#include <libkern/c++/OSNumber.h>

OSArray *getArrayWithNumbers(unsigned int count)
{
	OSArray *array = OSArray::withCapacity(count);
	if (array == nullptr)
		return nullptr;

	for (size_t n = 0; n < count; ++n) {
		OSNumber *number = OSNumber::withNumber(0ULL, 64);
		if (number == nullptr) {
			array->release();
			return nullptr;
		}
		array->setObject(number);
	}

	return array;
}
