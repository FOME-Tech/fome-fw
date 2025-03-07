#include "pch.h"
#include "proteus_meta.h"

/**
 * set engine_type 6
 */
void proteusHarley() {
	strcpy(engineConfiguration->scriptSettingName[0], "compReleaseRpm");
	engineConfiguration->scriptSetting[0] = 300;
	strcpy(engineConfiguration->scriptSettingName[1], "compReleaseDur");
	engineConfiguration->scriptSetting[1] = 5000;

	// for now we need non wired camInput to keep TS field enable/disable logic happy
	engineConfiguration->camInputs[0] = PROTEUS_DIGITAL_6;
	engineConfiguration->vvtMode[0] = VVT_MAP_V_TWIN;

	engineConfiguration->cylindersCount = 2;
	engineConfiguration->firingOrder = FO_1_2;

	engineConfiguration->mapCamDetectionAnglePosition = 50;

	engineConfiguration->luaOutputPins[0] = PROTEUS_LS_12;
#if HW_PROTEUS
	strncpy(config->luaScript, R"(
outputIndex = 0
startPwm(outputIndex, 100, 0)

rpmLimitSetting = findSetting("compReleaseRpm", 300)
compReleaseDulationLimit = findSetting("compReleaseDur", 6000)

every200msTimer = Timer.new();
everySecondTimer = Timer.new();
every50msTimer = Timer.new();
offCounter = 0
-- cranking!
packet542 = {0x20, 0x82, 0x81, 0xd9, 0x00, 0x00, 0x00, 0x00}
packet543 = {0x13, 0x57, 0x13, 0x45, 0x00, 0xe8, 0x00, 0x00}
packet541 = {0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0xFF}

-- every 200ms
packet540 = {0x00, 0x00, 0x5a, 0x4c, 0xff, 0x00, 0x00, 0x00}

-- every 1000ms
packet502 = {0x01}
packet546 = {0x35, 0x48, 0x44, 0x31, 0x46, 0x48}
packet547 = {0x50, 0x41, 0x31, 0x4b, 0x42, 0x36}
packet548 = {0x33, 0x34, 0x38, 0x32, 0x32, 0x00}

counter543 = 0;
setTickRate(66);

canRxAdd(0x570)
canRxAdd(0x500)

function onCanRx(bus, id, dlc, data)
  --print('got CAN id=' .. id .. ' dlc='  .. dlc)
  id11 = id % 2048

  if id11 == 0x500 then --Check can state of BCM
    canState = data[1]
    if canState == 01 then
      packet502[1] = 0x01
    else
      packet502[1] = 0x00
    end
    if id11 == 0x570 then
      curState = data[1]
      if curState == 06 then -- Cranking TODO: MUST ONLY DO THIS ON RPM TILL STARt
        packet542[2] =  0x82
      end
      if curState == 04 then -- Kill off
        packet542[2] =  0x82
      end
      if curState == 01 then -- Kill
        packet542[2] =  0xA2
      end
    end
  end
end


function onTick()
 
  if packet502[1] == 01 then
    offCounter = 0
    counter543 = (counter543 + 1) % 64
    packet543[7] = 64 + counter543
    packet543[8] = crc8_j1850(packet543, 7)
    APP = getSensor("AcceleratorPedal") 
    if APP == nil then
      packet543[5] = 0
    else
      packet543[5] = APP *2
    end
 
    txCan(1, 0x543, 0, packet543)
	txCan(1, 0x541, 0, packet541)
   
    if every200msTimer:getElapsedSeconds() > 0.2 then
       every200msTimer:reset();
       txCan(1, 0x540, 0, packet540)
    end

    if every50msTimer:getElapsedSeconds() > 0.05 then
       every50msTimer:reset();
	   txCan(1, 0x542, 0, packet542)
    end
	
    if everySecondTimer:getElapsedSeconds() > 1 then
       everySecondTimer:reset();
       txCan(1, 0x502, 0, packet502)
       txCan(1, 0x546, 0, packet546)
       txCan(1, 0x547, 0, packet547)
       txCan(1, 0x548, 0, packet548)
    end


	rpm = getSensor("RPM")
-- handle nil RPM, todo: change firmware to avoid nil RPM
	rpm = (rpm == nil and 0 or rpm)
    --print('Rpm ' .. rpm)
	--print('getTimeSinceTriggerEventMs ' .. getTimeSinceTriggerEventMs())
	enableCompressionReleaseSolenoid = getTimeSinceTriggerEventMs() < compReleaseDulationLimit and rpm < rpmLimitSetting
    duty = enableCompressionReleaseSolenoid and 1 or 0
    print("Compression release solenoid " .. duty)
	setPwmDuty(outputIndex, duty)
  else
    if offCounter == 0 then --goodbye sweet love
      txCan(1, 0x502, 0, packet502) --goodbye
      offCounter = 1 --One shot 
    end
  end
end
)", efi::size(config->luaScript));
#endif
}
