// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENATASK_HPP
#define _ENATASK_HPP

#include <libkern/c++/OSObject.h>

#include <IOKit/IOLib.h>

class ENATask : public OSObject
{
	OSDeclareAbstractStructors(ENATask)
public:
	typedef void (*Action)(OSObject *owner);

	static ENATask *withAction(OSObject *owner, Action action);

	bool init(OSObject *owner, Action action);

	void enable();
	void disable();

	void callTask();

private:
	OSObject		*_owner;
	IOLock			*_taskLock;
	IOSimpleLock		*_flagLock;
	IOThread		_taskThread;
	Action			_handler;
	bool			_workToDo;
	bool			_terminate;
	bool			_isEnabled;

	void free() APPLE_KEXT_OVERRIDE;

	void mainThread();

	void lockTask();
	void unlockTask();
};

/***********************************************************
 * Inline definitions
 ***********************************************************/

inline void ENATask::lockTask()
{
	IOLockLock(_taskLock);
}

inline void ENATask::unlockTask()
{
	IOLockUnlock(_taskLock);
}

inline void ENATask::enable()
{
	lockTask();
	_isEnabled = true;
	unlockTask();
}

inline void ENATask::disable()
{
	lockTask();
	_isEnabled = false;
	unlockTask();
}

inline void ENATask::callTask()
{
	if (_taskThread != nullptr) {
		IOInterruptState is = IOSimpleLockLockDisableInterrupt(_flagLock);
		_workToDo = true;
		thread_wakeup_one((void *)&_workToDo);
		IOSimpleLockUnlockEnableInterrupt(_flagLock, is);
	}
}

#endif // _ENATASK_HPP
