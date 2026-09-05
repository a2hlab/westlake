/*
 * Copyright (C) 2026 The Westlake Project
 *
 * Optional zygote class-list preload for the OpenHarmony Android runtime.
 */
package com.android.internal.os;

import java.io.BufferedReader;
import java.io.FileReader;
import java.lang.reflect.Method;

/**
 * Loads boot-classpath metadata in the parent appspawn process without running
 * class initializers. Android normally initializes its curated preloaded class
 * list in zygote. The standalone OH runtime cannot safely do that yet because a
 * missing service or native can poison a class for every forked child. Loading
 * with {@code initialize=false} still shares class metadata and populated dex
 * caches with children while preserving each class's normal initialization
 * point.
 */
final class WestlakePreloadClassList {
    private static final String ENV_PATH = "WESTLAKE_PRELOAD_CLASS_LIST";
    private static final int MAX_FAILURE_LOGS = 16;

    private WestlakePreloadClassList() {}

    static void run() {
        String path = System.getenv(ENV_PATH);
        if (path == null || path.isEmpty()) {
            return;
        }

        long startNanos = System.nanoTime();
        int loaded = 0;
        int failed = 0;
        try (BufferedReader reader = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                try {
                    Class.forName(line, false, null);
                    loaded++;
                } catch (Throwable t) {
                    failed++;
                    if (failed <= MAX_FAILURE_LOGS) {
                        System.err.println("[WESTLAKE-PRELOAD] skip " + line
                                + ": " + t.getClass().getName());
                    }
                }
            }
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-PRELOAD] cannot read " + path + ": " + t);
            return;
        }

        boolean dexCachesPopulated = false;
        try {
            Class<?> vmRuntimeClass = Class.forName("dalvik.system.VMRuntime", false, null);
            Method getRuntime = vmRuntimeClass.getDeclaredMethod("getRuntime");
            Method preloadDexCaches = vmRuntimeClass.getDeclaredMethod("preloadDexCaches");
            getRuntime.setAccessible(true);
            preloadDexCaches.setAccessible(true);
            Object runtime = getRuntime.invoke(null);
            preloadDexCaches.invoke(runtime);
            dexCachesPopulated = true;
        } catch (Throwable t) {
            System.err.println("[WESTLAKE-PRELOAD] preloadDexCaches unavailable: " + t);
        }

        long elapsedMillis = (System.nanoTime() - startNanos) / 1_000_000L;
        System.err.println("[WESTLAKE-PRELOAD] loaded=" + loaded
                + " failed=" + failed
                + " initialize=false"
                + " dexCaches=" + dexCachesPopulated
                + " elapsedMs=" + elapsedMillis);
    }
}
