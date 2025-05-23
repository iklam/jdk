/*
 * Copyright (c) 2019, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package jdk.jfr.api.consumer.streaming;

import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.atomic.AtomicInteger;

import jdk.jfr.consumer.EventStream;

/**
 * @test
 * @summary Test that a stream ends/closes when an application crashes.
 * @requires vm.flagless
 * @requires vm.hasJFR
 * @library /test/lib /test/jdk
 * @modules jdk.jfr jdk.attach java.base/jdk.internal.misc
 *
 * @run main/othervm -Dsun.tools.attach.attachTimeout=100000 jdk.jfr.api.consumer.streaming.TestJVMCrash
 */
public class TestJVMCrash {

    /*
     * We don't run if -esa is specified, because parser invariants are not
     * guaranteed for emergency dumps, which are provided on a best-effort basis only.
     */
    private static boolean hasIncompatibleTestOptions() {
        String testVmOpts[] = System.getProperty("test.vm.opts","").split("\\s+");
        for (String s : testVmOpts) {
            if (s.equals("-esa")) {
                System.out.println("Incompatible option: " + s + " specified. Skipping test.");
                return true;
            }
        }
        return false;
    }

    public static void main(String... args) {
        if (hasIncompatibleTestOptions()) {
            return;
        }

        int id = 1;
        while (true) {
            try (TestProcess process = new TestProcess("crash-application-" + id++, false /* createCore */))  {
                AtomicInteger eventCounter = new AtomicInteger();
                try (EventStream es = EventStream.openRepository(process.getRepository())) {
                    // Start from first event in repository
                    es.setStartTime(Instant.EPOCH);
                    es.onEvent(e -> {
                        if (eventCounter.incrementAndGet() == TestProcess.NUMBER_OF_EVENTS) {
                            process.crash();
                        }
                    });
                    es.startAsync();
                    // If crash corrupts chunk in repository, retry in 30 seconds
                    es.awaitTermination(Duration.ofSeconds(30));
                    if (eventCounter.get() == TestProcess.NUMBER_OF_EVENTS) {
                        return;
                    }
                    System.out.println("Incorrect event count. Retrying...");
                }
            } catch (Exception e) {
                System.out.println("Exception: " + e.getMessage());
                System.out.println("Retrying...");
            }
        }
    }
}
