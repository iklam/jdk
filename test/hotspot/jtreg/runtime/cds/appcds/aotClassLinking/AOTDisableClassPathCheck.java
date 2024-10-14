/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

/*
 * @test
 * @requires vm.cds
 * @summary Test the diagnostc flag -XX:+AOTDisableClassPathCheck
 * @library /test/jdk/lib/testlibrary
 *          /test/lib
 *          /test/hotspot/jtreg/runtime/cds/appcds
 *          /test/hotspot/jtreg/runtime/cds/appcds/test-classes
 * @build Hello
 * @run driver jdk.test.lib.helpers.ClassFileInstaller Hello
 * @run driver AOTDisableClassPathCheck
 */

import static java.util.stream.Collectors.*;

import jdk.test.lib.cds.CDSTestUtils;
import jdk.test.lib.helpers.ClassFileInstaller;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class AOTDisableClassPathCheck {
    static final String flag_UnlockDiagnosticVMOptions = "-XX:+UnlockDiagnosticVMOptions";
    static final String flag_AOTDisableClassPathCheck  = "-XX:+AOTDisableClassPathCheck";

    public static void main(String[] args) throws Exception {
        // (A) Simple HelloWorld
        String pwd = System.getProperty("user.dir", ".");
        TestCommon.testDump(pwd, TestCommon.list("Hello"),
                            "-Xlog:class+load",
                            "-Xlog:class+path",
                            "-Xlog:cds+class=debug",
                            flag_UnlockDiagnosticVMOptions,
                            flag_AOTDisableClassPathCheck);

        TestCommon.run("-cp", pwd,
                       "-Xlog:cds",
                       "-Xlog:class+load",
                       "-Xlog:class+path",
                       flag_UnlockDiagnosticVMOptions,
                       flag_AOTDisableClassPathCheck,
                       "Hello")
            .assertNormalExit("Hello source: shared objects file");

        // (B) A more complex program with lambdas, etc.
        //     Note that we don't specify the -cp, as it is specified by
        //     ProcessTools.createLimitedTestJavaProcessBuilder() to include
        //     the classfiles built by jtreg for AOTDisableClassPathCheck.java
        String aotConfigFile = "test.aotconfig";
        String mainClass = AOTDisableClassPathCheckApp.class.getName();

        // (B1) Training Run
        ProcessBuilder pb = ProcessTools.createLimitedTestJavaProcessBuilder(
             "-XX:AOTMode=record",
             "-XX:AOTConfiguration=" + aotConfigFile,
             mainClass);

        OutputAnalyzer out = CDSTestUtils.executeAndLog(pb, "train");
        out.shouldContain("Hello AOTDisableClassPathCheckApp");
        out.shouldContain("stream: hello, world");
        out.shouldHaveExitValue(0);

        for (int i = 0; i < 2; i++) {
            String aotCacheFile = "test" + i + ".aot";
            // (B2) Assembly Phase
            pb = ProcessTools.createLimitedTestJavaProcessBuilder(
                "-XX:AOTMode=create",
                "-XX:AOTConfiguration=" + aotConfigFile,
                "-XX:AOTCache=" + aotCacheFile,
                "-XX:" + (i == 0 ? "-" : "+") + "AOTClassLinking",
                flag_UnlockDiagnosticVMOptions,
                flag_AOTDisableClassPathCheck,
                "-Xlog:cds+class=debug",
                "-Xlog:cds");

            out = CDSTestUtils.executeAndLog(pb, "asm");
            out.shouldContain("Dumping shared data to file:");
            out.shouldHaveExitValue(0);

            if (i == 1) {
                // Should have at least one archived indy
                out.shouldMatch("Indy *CP entries.*archived = *[1-9]");
            }

            // (B3) Production Run with AOTCache
            pb = ProcessTools.createLimitedTestJavaProcessBuilder(
                "-XX:AOTCache=" + aotCacheFile,
                "-Xlog:cds",
                "-Xlog:class+load",
                "-Xlog:class+path",
                flag_UnlockDiagnosticVMOptions,
                flag_AOTDisableClassPathCheck,
                mainClass);
            out = CDSTestUtils.executeAndLog(pb, "prod");
            out.shouldContain("Opened archive " + aotCacheFile);
            out.shouldContain("Hello AOTDisableClassPathCheckApp");
            out.shouldContain("stream: hello, world");
            out.shouldContain("AOTDisableClassPathCheckApp source: shared objects file");
            out.shouldContain("use_full_module_graph = true");
            out.shouldHaveExitValue(0);
            if (i == 1) {
                out.shouldContain("Using AOT-linked classes: true (static archive: has aot-linked classes)");
            }
        }
    }
}

class AOTDisableClassPathCheckApp {
    public static void main(String args[]) {
        System.out.println("Hello " + AOTDisableClassPathCheckApp.class.getName());
        String cp = System.getProperty("java.class.path");
        System.out.println("java.class.path = " + cp);
        String expect = "/AOTDisableClassPathCheck.d";
        if (cp.indexOf(expect) < 0) {
            throw new RuntimeException("java.class.path should contain \"" + expect + "\"");
        }

        doit(() -> {
                var words = java.util.List.of("hello", "fuzzy", "world");
                System.out.println("stream: " + words.stream().filter(w->!w.contains("u")).collect(joining(", ")));
                // => stream: hello, world
            });
    }

    static void doit(Runnable r) {
        r.run();
    }
}
