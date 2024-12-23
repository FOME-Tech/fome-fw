/*
 * @file test_pid_auto.cpp
 *
 * @date Sep 29, 2019
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

// see also idle.timingPid test

#include "pch.h"

#include "efi_pid.h"

TEST(util, pid) {
	pid_s pidS;
	pidS.pFactor = 50;
	pidS.iFactor = 0.5;
	pidS.dFactor = 0;
	pidS.offset = 0;
	pidS.minValue = 10;
	pidS.maxValue = 90;

	Pid pid(&pidS);

	ASSERT_FLOAT_EQ( 90,  pid.getOutput(14, 12, 0.1)) << "getValue#90";


	ASSERT_FLOAT_EQ( 10,  pid.getOutput(14, 16, 0.1)) << "getValue#10";
	ASSERT_FLOAT_EQ(10, pid.getOutput(14, 16, 1));

	pidS.pFactor = 29;
	pidS.iFactor = 0;
	pidS.dFactor = 0;
	ASSERT_FLOAT_EQ(10, pid.getOutput(14, 16, 1));
//	ASSERT_FLOAT_EQ(68, pid.getIntegration());

	ASSERT_FLOAT_EQ(10, pid.getOutput(14, 16, 1));
//	ASSERT_FLOAT_EQ(0, pid.getIntegration());

	ASSERT_FLOAT_EQ(10, pid.getOutput(14, 16, 1));
//	ASSERT_FLOAT_EQ(68, pid.getIntegration());



	pidS.pFactor = 1;
	pidS.iFactor = 0;
	pidS.dFactor = 0;
	pidS.offset = 0;
	pidS.minValue = 0;
	pidS.maxValue = 100;

	pid.reset();

	ASSERT_FLOAT_EQ( 50,  pid.getOutput(/*target*/50, /*input*/0, 0.005)) << "target=50, input=0";
	ASSERT_FLOAT_EQ( 0,  pid.iTerm) << "target=50, input=0 iTerm";

	ASSERT_FLOAT_EQ( 0,  pid.getOutput(/*target*/50, /*input*/70, 0.005)) << "target=50, input=70";
	ASSERT_FLOAT_EQ( 0,  pid.iTerm) << "target=50, input=70 iTerm";

	ASSERT_FLOAT_EQ( 0,  pid.getOutput(/*target*/50, /*input*/70, 0.005)) << "target=50, input=70 #2";
	ASSERT_FLOAT_EQ( 0,  pid.iTerm) << "target=50, input=70 iTerm #2";

	ASSERT_FLOAT_EQ( 0,  pid.getOutput(/*target*/50, /*input*/50, 0.005)) << "target=50, input=50";
	ASSERT_FLOAT_EQ( 0,  pid.iTerm) << "target=50, input=50 iTerm";
}

static void commonPidTestParameters(pid_s * pidS) {
	pidS->pFactor = 0;
	pidS->iFactor = 50;
	pidS->dFactor = 0;
	pidS->offset = 0;
	pidS->minValue = 10;
	pidS->maxValue = 40;
}

static void commonPidTest(Pid *pid) {
	pid->iTermMax = 45;

	ASSERT_FLOAT_EQ( 12.5,  pid->getOutput(/*target*/50, /*input*/0, 0.005f)) << "target=50, input=0 #0";
	ASSERT_FLOAT_EQ( 12.5,  pid->getIntegration());
	ASSERT_FLOAT_EQ( 25  ,  pid->getOutput(/*target*/50, /*input*/0, 0.005f)) << "target=50, input=0 #1";

	ASSERT_FLOAT_EQ( 37.5,  pid->getOutput(/*target*/50, /*input*/0, 0.005f)) << "target=50, input=0 #2";
	ASSERT_FLOAT_EQ( 37.5,  pid->getIntegration());

	ASSERT_FLOAT_EQ( 40.0,  pid->getOutput(/*target*/50, /*input*/0, 0.005f)) << "target=50, input=0 #3";
	ASSERT_FLOAT_EQ( 45,    pid->getIntegration());
}

TEST(util, parallelPidLimits) {
	pid_s pidS;
	commonPidTestParameters(&pidS);

	Pid pid(&pidS);
	commonPidTest(&pid);
}

TEST(util, industrialPidLimits) {
	pid_s pidS;
	commonPidTestParameters(&pidS);
}
