// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef ENA_PLAT_H_
#define ENA_PLAT_H_


#if defined(ENA_IPXE)
#include <ena_plat_ipxe.h>
#elif defined(__linux__)
#include <ena_plat_linux.h>
#elif defined(_WIN32)
#include <ena_plat_windows.h>
#elif defined(__FreeBSD__)
#include <ena_plat_fbsd.h>
#elif defined(__APPLE__)
#include <ena_plat_macos.h>
#else
#error "Invalid platform"
#endif

#endif /* ENA_PLAT_H_ */
