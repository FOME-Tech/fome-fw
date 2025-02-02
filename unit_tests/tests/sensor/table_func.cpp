#include "pch.h"

#include "table_func.h"

TEST(TableFuncTest, basic) {
	float in[] = { 0, 10 };
	float out[] = { 30, 40 };

	TableFunc dut(in, out);

	EXPECT_EQ(30, dut.convert(-10).value_or(0));
	EXPECT_EQ(30, dut.convert(0).value_or(0));
	EXPECT_EQ(35, dut.convert(5).value_or(0));
	EXPECT_EQ(40, dut.convert(10).value_or(0));
	EXPECT_EQ(40, dut.convert(20).value_or(0));
}
