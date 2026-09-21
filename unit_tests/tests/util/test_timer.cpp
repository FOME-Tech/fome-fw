#include "pch.h"
#include "efi_timer.h"

TEST(util, timer) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	Timer timer;
	ASSERT_TRUE(timer.hasElapsedSec(3));
	timer.reset();
	ASSERT_FALSE(timer.hasElapsedSec(3));

	eth.moveTimeForwardSec(4);
	ASSERT_TRUE(timer.hasElapsedSec(3));
}

TEST(util, timerResetInFuture) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	Timer timer;

	// Simulate an interrupt resetting the timer between the reader sampling
	// the current time and reading the reset time: the reset is slightly in
	// the "future" relative to now. That must not look like a huge elapsed time.
	timer.reset(getTimeNowNt() + US2NT(10));

	EXPECT_FALSE(timer.hasElapsedUs(1));
	EXPECT_FALSE(timer.hasElapsedSec(2));
	EXPECT_EQ(0, timer.getElapsedUs());
}
