package com.rusefi.maintenance;

public interface OpenbltCallbacks
{
    void log(String line);
    void updateProgress(int percent);
    void error(String line);

    void setPhase(String title, boolean hasProgress);
}
