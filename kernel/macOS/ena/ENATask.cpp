// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ENATask.hpp"

#define super OSObject

OSDefineMetaClassAndStructors(ENATask, super)

ENATask * ENATask::withAction(OSObject *owner, Action action)
{
	ENATask *task = new ENATask;

	if (task != nullptr && !task->init(owner, action)) {
		task->release();
		return nullptr;
	}

	return task;
}

bool ENATask::init(OSObject *owner, Action action)
{
	if (!super::init())
		return false;

	_isEnabled = false;
	_flagLock = IOSimpleLockAlloc();
	if (_flagLock == nullptr)
		return false;
	IOSimpleLockInit(_flagLock);

	_taskLock = IOLockAlloc();
	if (_taskLock == nullptr)
		return false;

	_owner = owner;
	_handler = action;

	_terminate = false;
	_workToDo = false;
	thread_continue_t cptr = OSMemberFunctionCast(thread_continue_t, this, &ENATask::mainThread);
	if (kernel_thread_start(cptr, this, &_taskThread) != KERN_SUCCESS)
		return false;

	return true;
}

void ENATask::free()
{
	if (_taskThread != nullptr) {
		// First entry for the free, called by release(). Perform cleanup
		// of the working thread by setting terminate flag.
		disable();
		lockTask();
		IOInterruptState is = IOSimpleLockLockDisableInterrupt(_flagLock);
		_terminate = true;
		thread_wakeup_one((void *)&_workToDo);
		IOSimpleLockUnlockEnableInterrupt(_flagLock, is);
		unlockTask();
	} else {
		// The _taskThread is calling free for the second time, as it is
		// now safe to release rest of the resources when the main thread
		// is done. The second case is partial initialization.
		if (_taskLock) {
			IOLockFree(_taskLock);
			_taskLock = nullptr;
		}

		if (_flagLock != nullptr) {
			IOSimpleLockFree(_flagLock);
			_flagLock = nullptr;
		}

		super::free();
	}
}

void ENATask::mainThread()
{
	// Execute handler as long as there is work to be done
	while (!_terminate) {
		IOInterruptState is = IOSimpleLockLockDisableInterrupt(_flagLock);
		_workToDo = false;
		IOSimpleLockUnlockEnableInterrupt(_flagLock, is);

		lockTask();
		if (_isEnabled)
			_handler(_owner);
		unlockTask();

		is = IOSimpleLockLockDisableInterrupt(_flagLock);
		if (!_terminate && !_workToDo) {
			// Declare, that the thread is going to wait on _workToDo event
			assert_wait((event_t *) &_workToDo, THREAD_UNINT);
			IOSimpleLockUnlockEnableInterrupt(_flagLock, is);

			thread_continue_t cptr = NULL;

			cptr = OSMemberFunctionCast(
				thread_continue_t, this, &ENATask::mainThread);
			// Reschedule process and wait for the event - when the
			// event occurs, it will discard current kernel stack
			// of the thread and run below function
			thread_block_parameter(cptr, this);
			/* NOT REACHED */
		}

		// At this point there is either more work to be done or thread
		// must commit suicide. But no matter what, clear the simple
		// lock and retore the interrupt state
		IOSimpleLockUnlockEnableInterrupt(_flagLock, is);
	}

	thread_t thread = _taskThread;
	_taskThread = nullptr;
	// Commit suicide - the second call of free from thread context is
	// cleaning up rest of the resources.
	free();

	thread_deallocate(thread);
	thread_terminate(thread);
}
