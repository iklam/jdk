/*
 * Copyright (c) 2026, Oracle and/or its affiliates. All rights reserved.
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

/**
 * @test
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/cds/appcds /test/hotspot/jtreg/runtime/cds/appcds/test-classes
 * @build Hello
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar app.jar Hello
 * @run driver ChangedModulePath
 */

import java.nio.file.Path;
import java.nio.file.Paths;
import jdk.test.lib.cds.CDSModulePackager;
import jdk.test.lib.cds.CDSTestUtils;
import jdk.test.lib.process.OutputAnalyzer;

public class ChangedModulePath {

    static final Path modulesSrc1 = Paths.get(System.getProperty("test.src")).resolve("modules1");
    static final Path modulesSrc2 = Paths.get(System.getProperty("test.src")).resolve("modules2");
    static String modulePath1;
    static String modulePath2;

    public static void main(String[] args) throws Exception {
        CDSModulePackager modulePackager1 = new CDSModulePackager(modulesSrc1, Paths.get("test-modules1"));
        modulePackager1.createModularJar("com.test");
        modulePath1 = modulePackager1.getOutputDir().toString();

        CDSModulePackager modulePackager2 = new CDSModulePackager(modulesSrc2, Paths.get("test-modules2"));
        modulePackager2.createModularJar("com.test");
        modulePath2 = modulePackager2.getOutputDir().toString();

        String mainClass =  "com.test.Foo";
        String appJar = "app.jar"; // Dummy. We are not loading classes from here
        String appClasses[] = {mainClass};

        OutputAnalyzer output = TestCommon.createArchive(
                                        appJar, appClasses,
                                        "-Xlog:cds+class=debug",
                                        "--add-modules=com.test",
                                        "--module-path", modulePath1.toString());
        TestCommon.checkDump(output);
        output.shouldMatch("klasses.* app .*com.test.Foo");

        TestCommon.run("-cp", appJar,
                       "--add-modules=com.test",
                       "--module-path", modulePath1.toString(),
                       mainClass)
          .assertNormalExit("Foo.java from modules1/");

        TestCommon.runWithoutCDS("-cp", appJar,
                       "--add-modules=com.test",
                       "--module-path", modulePath2.toString(),
                       mainClass)
          .assertNormalExit("Foo.java from modules2/");

        TestCommon.run("-cp", appJar,
                       "-Xlog:cds",
                       "-Xlog:class+load",
                       "-Xlog:class+path",
                       "--add-modules=com.test",
                       "--module-path", modulePath2.toString(),
                       mainClass)
          .assertNormalExit("Foo.java from modules2/");
    }
}
