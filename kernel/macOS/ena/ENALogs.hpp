// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENA_LOGS_HPP
#define _ENA_LOGS_HPP

#include <IOKit/IOLib.h>

// General purpose logging macros, can be used from any context.
#define DEV_LOG(label, format, ...) IOLog(OS_STRINGIFY(label)": " format, ##__VA_ARGS__)

// Error log is currently just wrapper around normal log
#define ERROR_LOG(label, format, ...) DEV_LOG(label, format, ##__VA_ARGS__)

// Enable debug logs only in the Debug configuration
#ifdef DEBUG
#define DEBUG_LOG(label, format, ...) DEV_LOG(label, format, ##__VA_ARGS__)
#else
#define DEBUG_LOG(label, format, ...) do {} while (0);
#endif

// ENA specific logs - they should be used only from the AmazonENAController
// context (ENA_*_LOG macros) or by the classes that have reference to the
// AmazonENAController object (ENA_*_LOG_EXT macros).
// In some cases, the ENA object may not have valid name. In that situation,
// the ENA_*_LOGC macros should be used, which adds constant label before the
// log message.

#ifndef ENA
#define ENA AmazonENAEthernet
#endif

#define ENA_LOG(format, ...) IOLog("%s: " format, getName(), ##__VA_ARGS__)
#define ENA_LOG_EXT(service, format, ...) IOLog("%s: " format, (service)->getName(), ##__VA_ARGS__)
#define ENA_LOGC(format, ...) DEV_LOG(ENA, format, ##__VA_ARGS__)

// Error and Debug logs have the same meaning as generic macros above.
#define ENA_ERROR_LOG(format, ...) ENA_LOG(format, ##__VA_ARGS__)
#define ENA_ERROR_LOG_EXT(service, format, ...)	ENA_LOG_EXT(service, format, ##__VA_ARGS__)
#define ENA_ERROR_LOGC(format, ...) ENA_LOGC(format, ##__VA_ARGS__)

#ifdef DEBUG
#define ENA_DEBUG_LOG(format, ...) ENA_LOG(format, ##__VA_ARGS__)
#define ENA_DEBUG_LOG_EXT(service, format, ...) ENA_LOG_EXT(service, format, ##__VA_ARGS__)
#define ENA_DEBUG_LOGC(format, ...) ENA_LOGC(format, ##__VA_ARGS__)
#else
#define ENA_DEBUG_LOG(format, ...) do {} while(0)
#define ENA_DEBUG_LOG_EXT(service, format, ...) do {} while(0)
#define ENA_DEBUG_LOGC(format, ...) do {} while(0)
#endif

#endif // ENA_LOGS_HPP
