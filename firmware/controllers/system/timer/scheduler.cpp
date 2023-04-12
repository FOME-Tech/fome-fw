/**
 * @file	scheduler.h
 *
 * @date October 1, 2020
 */
#include "pch.h"

#include "scheduler.h"

void action_s::execute() {
	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT, callback != NULL, "callback==null1");
	callback(param);
}

schfunc_t action_s::getCallback() const {
	return callback;
}

void * action_s::getArgument() const {
	return param;
}
