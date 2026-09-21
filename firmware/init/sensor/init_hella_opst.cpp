#include "pch.h"

#include "init.h"
#include "hella_opst.h"

static HellaOpsTSensor hellaOpsT;

void initHellaOpsT() {
	hellaOpsT.init(engineConfiguration->hellaOpsTInput);
}

void deinitHellaOpsT() {
	hellaOpsT.deInit();
}
