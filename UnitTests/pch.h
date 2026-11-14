//
// pch.h
// Header for standard system include files.
//

#pragma once

#include "IgnoreWarning.h"
#include <cstdio>
#include <gtest/gtest.h>

inline FILE* OpenTestFile(const char* filename, const char* mode)
{
#ifdef _WIN32
	FILE* fp = nullptr;
	return fopen_s(&fp, filename, mode) == 0 ? fp : nullptr;
#else
	return std::fopen(filename, mode);
#endif
}

#ifdef _MSC_VER
	#define NPY_IGNORE_SWITCH_DEFAULT_WARNING()
#else
	#define NPY_IGNORE_SWITCH_DEFAULT_WARNING() __IGNORE_WARNING__("-Wswitch-default")
#endif

#define NPY_EXPECT_DEATH(statement, regex)         \
	do                                             \
	{                                              \
		__START_IGNORING_WARNINGS__                \
		NPY_IGNORE_SWITCH_DEFAULT_WARNING()        \
		EXPECT_DEATH(statement, regex);            \
		__STOP_IGNORING_WARNINGS__                 \
	} while (false)
