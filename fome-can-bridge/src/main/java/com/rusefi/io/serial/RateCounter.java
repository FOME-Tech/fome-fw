package com.rusefi.io.serial;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Simple counter for rate tracking — slimmed down from the original.
 */
public class RateCounter {
    private final AtomicInteger count = new AtomicInteger();

    public void add() {
        count.incrementAndGet();
    }

    public int getCurrentRate() {
        return count.get();
    }

    @Override
    public String toString() {
        return String.valueOf(count.get());
    }
}
