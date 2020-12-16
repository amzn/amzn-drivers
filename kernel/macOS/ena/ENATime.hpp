// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef _ENATIME_HPP
#define _ENATIME_HPP

#include <kern/clock.h>

#include <libkern/OSTypes.h>

class ENATime
{
public:
	static ENATime getNow();

	ENATime() = default;
	ENATime(AbsoluteTime time);

	ENATime & setNow();
	ENATime & addAbsoluteTimeFromNow(AbsoluteTime time);
	ENATime & addSecondsFromNow(UInt64 seconds);
	ENATime & addMillisecondsFromNow(UInt64 milliseconds);
	ENATime & addMicrosecondsFromNow(UInt64 microseconds);

	ENATime & addAbsoluteTime(AbsoluteTime time);
	ENATime & addSeconds(UInt64 seconds);
	ENATime & addMilliseconds(UInt64 milliseconds);
	ENATime & addMicroseconds(UInt64 microseconds);

	ENATime & setAbsoluteTime(UInt64 time);
	ENATime & setSeconds(UInt64 seconds);
	ENATime & setMilliseconds(UInt64 milliseconds);
	ENATime & setMicroseconds(UInt64 microseconds);

	AbsoluteTime getAbsoluteTime();
	uint64_t getSeconds();
	uint64_t getMilliseconds();
	uint64_t getMicroseconds();

	ENATime & operator=(AbsoluteTime time);

	ENATime & operator+=(const ENATime &other);
	ENATime & operator-=(const ENATime &other);
	ENATime & operator*=(const ENATime &other);

	ENATime operator+(const ENATime &second) const;
	ENATime operator-(const ENATime &second) const;
	ENATime operator*(int value) const;

	bool operator>(const ENATime &second) const;
	bool operator>=(const ENATime &second) const;
	bool operator<(const ENATime &second) const;
	bool operator<=(const ENATime &second) const;
	bool operator==(const ENATime &second) const;
	bool operator==(AbsoluteTime absoluteTime) const;
	bool operator!=(const ENATime &second) const;

private:
	AbsoluteTime _time;
};

/***********************************************************
 * Interface functions
 ***********************************************************/

bool operator==(AbsoluteTime absoluteTime, const ENATime &enaTime);
ENATime operator*(int value, const ENATime &time);

/***********************************************************
 * Inline definitions
 ***********************************************************/

inline ENATime::ENATime(AbsoluteTime time) : _time(time)
{

}

inline ENATime ENATime::getNow()
{
	return ENATime(mach_absolute_time());
}

inline ENATime & ENATime::setNow()
{
	_time = mach_absolute_time();
	return *this;
}

inline ENATime & ENATime::addAbsoluteTimeFromNow(AbsoluteTime time)
{
	_time = mach_absolute_time() + time;
	return *this;
}

inline ENATime & ENATime::addSecondsFromNow(UInt64 seconds)
{
	addMillisecondsFromNow(seconds*1000);
	return *this;
}

inline ENATime & ENATime::addMillisecondsFromNow(UInt64 milliseconds)
{
	addMicrosecondsFromNow(milliseconds*1000);
	return *this;
}

inline ENATime & ENATime::addMicrosecondsFromNow(UInt64 microseconds)
{
	nanoseconds_to_absolutetime(microseconds*1000, &_time);
	_time += mach_absolute_time();
	return *this;
}

inline ENATime & ENATime::setAbsoluteTime(UInt64 time)
{
	_time = time;
	return *this;
}

inline ENATime & ENATime::setSeconds(UInt64 seconds)
{
	setMilliseconds(seconds*1000);
	return *this;
}

inline ENATime & ENATime::setMilliseconds(UInt64 milliseconds)
{
	setMicroseconds(milliseconds*1000);
	return *this;
}

inline ENATime & ENATime::setMicroseconds(UInt64 microseconds)
{
	nanoseconds_to_absolutetime(microseconds*1000, &_time);
	return *this;
}

inline UInt64 ENATime::getAbsoluteTime()
{
	return _time;
}

inline UInt64 ENATime::getSeconds()
{
	return getMilliseconds()/1000;
}

inline UInt64 ENATime::getMilliseconds()
{
	return getMicroseconds()/1000;
}

inline UInt64 ENATime::getMicroseconds()
{
	UInt64 microseconds;
	absolutetime_to_nanoseconds(_time, &microseconds);

	// Convert nanoseconds to microseconds
	microseconds /= 1000;
	return microseconds;
}

inline ENATime & ENATime::operator+=(const ENATime &other)
{
	_time += other._time;
	return *this;
}

inline ENATime & ENATime::operator-=(const ENATime &other)
{
	_time -= other._time;
	return *this;
}

inline ENATime & ENATime::operator*=(const ENATime &other)
{
	_time *= other._time;
	return *this;
}

inline ENATime & ENATime::operator=(AbsoluteTime time)
{
	_time = time;
	return *this;
}

inline bool ENATime::operator>(const ENATime &second) const
{
	return !(*this <= second);
}

inline bool ENATime::operator>=(const ENATime &second) const
{
	return !(*this < second);
}

inline bool ENATime::operator<(const ENATime &second) const
{
	return _time < second._time;
}

inline bool ENATime::operator<=(const ENATime &second) const
{
	return (*this < second || *this == second);
}

inline bool ENATime::operator==(const ENATime &second) const
{
	return _time == second._time;
}

inline bool ENATime::operator==(AbsoluteTime absoluteTime) const
{
	return _time == absoluteTime;
}

inline bool ENATime::operator!=(const ENATime &second) const
{
	return !(*this == second);
}

inline ENATime ENATime::operator+(const ENATime &second) const
{
	return ENATime(_time + second._time);
}

inline ENATime ENATime::operator-(const ENATime &second) const
{
	return ENATime(_time - second._time);
}

inline ENATime ENATime::operator*(int value) const
{
	return ENATime(_time * value);
}

inline bool operator==(AbsoluteTime absoluteTime, const ENATime &enaTime)
{
	return enaTime == absoluteTime;
}

inline ENATime operator*(int value, const ENATime &time)
{
	return time * value;
}

#endif // _ENATIME_HPP
