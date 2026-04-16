/*
 * !++
 * QDS - Quick Data Signalling Library
 * !-
 * Copyright (C) 2002 - 2020 Devexperts LLC
 * !-
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 * !__
 */
package com.devexperts.logging;

import java.util.concurrent.ConcurrentHashMap;

/**
 * Simplified ThreadNameFormatter — returns thread names as-is.
 * The original used IndexedSet for complex pattern-based renaming; bridge doesn't need that.
 */
class ThreadNameFormatter implements Comparable<ThreadNameFormatter> {

	private static final ConcurrentHashMap<String, String> NAME_CACHE = new ConcurrentHashMap<>();

	static String formatThreadName(long time, String threadName) {
		return NAME_CACHE.computeIfAbsent(threadName, k -> k);
	}

	final String thread_name;
	final String replacement_name;
	long last_time;

	ThreadNameFormatter(String thread_name, String replacement_name) {
		this.thread_name = thread_name;
		this.replacement_name = replacement_name;
	}

	public int compareTo(ThreadNameFormatter o) {
		return Long.compare(last_time, o.last_time);
	}
}
