/**
 * @file main.cpp
 * @file Unit tests (and some integration tests to be fair) of rusEFI
 *
 * @author Andrey Belomutskiy, (c) 2012-2021
 */

#include "pch.h"
#include <stdlib.h>

bool hasInitGtest = false;

GTEST_API_ int main(int argc, char** argv) {
	hasInitGtest = true;

	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
