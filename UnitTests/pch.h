//
// pch.h
// Header for standard system include files.
//

#pragma once

#include "../IgnoreWarning.h"
#include <gtest/gtest.h>

#define NPY_EXPECT_DEATH(statement, regex)         \
	do                                             \
	{                                              \
		__START_IGNORING_WARNINGS__                \
		__IGNORE_WARNING__("-Wswitch-default")     \
		EXPECT_DEATH(statement, regex);            \
		__STOP_IGNORING_WARNINGS__                 \
	} while (false)
