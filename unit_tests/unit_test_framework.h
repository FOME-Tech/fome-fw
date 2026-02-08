/*
 * @file unit_test_framework.h
 *
 * @date Mar 4, 2018
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#pragma once

using ::testing::Return;

/**
 * These are usually used with ASSETR_NEAR(val1, val2, abs_error)
 */
#define EPS0D 1
#define EPS1D 0.1
#define EPS2D 0.01
#define EPS3D 0.001
#define EPS4D 0.0001
#define EPS5D 0.00001

#define EXPECT_NO_FATAL_ERROR EXPECT_NO_THROW
#define EXPECT_FATAL_ERROR(expr) EXPECT_THROW((expr), std::logic_error)
