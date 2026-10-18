#include "pch.h"
#include "configuration_write.h"

TEST(ConfigurationWriteState, FailedAttemptNeedsANewRequestForAutomaticRetry) {
	ConfigurationWriteState state;
	EXPECT_FALSE(state.begin(true));
	state.request();
	ASSERT_TRUE(state.begin(true));
	EXPECT_TRUE(state.pending());
	EXPECT_TRUE(state.writing());
	EXPECT_FALSE(state.begin());
	EXPECT_FALSE(state.beginRead());
	state.complete(false);
	EXPECT_TRUE(state.pending());
	EXPECT_FALSE(state.writing());
	EXPECT_FALSE(state.shouldWrite());
	EXPECT_FALSE(state.begin(true));
	state.request();
	ASSERT_TRUE(state.begin(true));
	state.complete(true);
	EXPECT_FALSE(state.pending());
}

TEST(ConfigurationWriteState, RequestDuringWriteSurvivesBothSuccessAndFailure) {
	for (bool success : {false, true}) {
		ConfigurationWriteState state;
		ASSERT_TRUE(state.begin());
		state.request();
		EXPECT_FALSE(state.shouldWrite());
		state.complete(success);
		EXPECT_TRUE(state.pending());
		EXPECT_TRUE(state.shouldWrite());
		ASSERT_TRUE(state.begin(true));
		state.complete(true);
		EXPECT_FALSE(state.pending());
	}
}

TEST(ConfigurationWriteState, ReadSerializesDirectAndRequestedWrites) {
	ConfigurationWriteState state;
	ASSERT_TRUE(state.beginRead());
	EXPECT_TRUE(state.reading());
	EXPECT_FALSE(state.beginRead());
	state.request();
	EXPECT_FALSE(state.begin());
	EXPECT_FALSE(state.begin(true));
	EXPECT_FALSE(state.shouldWrite());
	state.endRead();
	EXPECT_TRUE(state.shouldWrite());
	ASSERT_TRUE(state.begin(true));
	state.complete(true);
	EXPECT_FALSE(state.pending());
}
