// This file is not named lua.h as it would collide with firmware/ext/lua/src/lua.h

#pragma once

#include "lua.hpp"

class LuaHandle final {
public:
	LuaHandle() : LuaHandle(nullptr) { }
	LuaHandle(lua_State* ptr) : m_ptr(ptr) { }

	// Don't allow copying!
	LuaHandle(const LuaHandle&) = delete;
	LuaHandle& operator=(const LuaHandle&) = delete;

	// Allow moving!
	LuaHandle(LuaHandle&& rhs) {
		m_ptr = rhs.m_ptr;
		rhs.m_ptr = nullptr;
	}

	// Move assignment operator
	LuaHandle& operator=(LuaHandle&& rhs) {
		m_ptr = rhs.m_ptr;
		rhs.m_ptr = nullptr;

		return *this;
	}

	// Destruction cleans up lua state
	~LuaHandle() {
		if (m_ptr) {
			efiPrintf("LUA: Tearing down instance...");
			lua_close(m_ptr);
		}
	}

	operator lua_State*() const { return m_ptr; }

private:
	lua_State* m_ptr;
};

void startLua();

#if EFI_UNIT_TEST
#include <rusefi/expected.h>

expected<float> testLuaReturnsNumberOrNil(const char* script);
float testLuaReturnsNumber(const char* script);
int testLuaReturnsInteger(const char* script);
void testLuaExecString(const char* script);
#endif

#if EFI_CAN_SUPPORT

#include "can.h"

// Lua CAN rx feature
void initLuaCanRx();

// Called from the Lua loop to process any pending CAN frames
void doLuaCanRx(LuaHandle& ls);
// Called from the CAN RX thread to queue a frame for Lua consumption
void processLuaCan(CanBusIndex busIndex, const CANRxFrame& frame);
#endif // not EFI_CAN_SUPPORT
