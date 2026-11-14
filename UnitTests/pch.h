//
// pch.h
// Header for standard system include files.
//

#pragma once

#include "../IgnoreWarning.h"
#include <gtest/gtest.h>

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
