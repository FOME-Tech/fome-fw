package com.rusefi;

import org.junit.runner.JUnitCore;
import org.junit.runner.Request;
import org.junit.runner.Result;

/**
 * Run chosen HW CI test classes or single test methods, for iterating on a local rig:
 *
 *   java -cp autotest-all.jar com.rusefi.HwCiRun com.rusefi.common.MiscTest
 *   java -cp autotest-all.jar com.rusefi.HwCiRun com.rusefi.common.MiscTest#testSetDate
 *
 * A bare class name without a package is looked up in the usual test packages.
 */
public class HwCiRun {
    private static final String[] PACKAGES = {
            "com.rusefi.common.",
            "com.rusefi.proteus.",
            "com.rusefi.atlas.",
            "com.rusefi.f4discovery.",
            "com.rusefi.",
    };

    public static void main(String[] args) throws ClassNotFoundException {
        if (args.length == 0) {
            System.err.println("Usage: HwCiRun <TestClass>[#method] ...");
            System.exit(1);
        }

        JUnitCore junit = new JUnitCore();
        boolean isOk = true;
        for (String arg : args) {
            String[] parts = arg.split("#", 2);
            Class<?> clazz = findClass(parts[0]);
            Request request = parts.length == 2 ? Request.method(clazz, parts[1]) : Request.aClass(clazz);

            Result result = junit.run(request);
            System.out.println(arg + ": ran " + result.getRunCount() + ", ignored " + result.getIgnoreCount()
                    + ", failed " + result.getFailureCount());
            result.getFailures().forEach(f -> {
                System.out.println("Test failed: " + f.getTestHeader() + " because " + f.getMessage());
                f.getException().printStackTrace();
            });
            isOk &= result.getRunCount() > 0 && result.getFailureCount() == 0;
        }
        System.exit(isOk ? 0 : -1);
    }

    private static Class<?> findClass(String name) throws ClassNotFoundException {
        if (name.contains("."))
            return Class.forName(name);
        for (String pkg : PACKAGES) {
            try {
                return Class.forName(pkg + name);
            } catch (ClassNotFoundException ignored) {
            }
        }
        throw new ClassNotFoundException(name);
    }
}
