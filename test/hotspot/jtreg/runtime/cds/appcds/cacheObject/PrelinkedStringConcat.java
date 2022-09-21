/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
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
 * @bug 8214781 8293187
 * @summary Test for the -XX:ArchiveHeapTestClass flag
 * @requires vm.cds.write.archived.java.heap
 * @modules java.base/sun.invoke.util java.logging
 * @library /test/jdk/lib/testlibrary /test/lib
 *          /test/hotspot/jtreg/runtime/cds/appcds
 *          /test/hotspot/jtreg/runtime/cds/appcds/test-classes
 * @build PrelinkedStringConcat Hello
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar boot.jar
 *             ConcatA
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar app.jar Hello
 * @run driver PrelinkedStringConcat
 */

import jdk.test.lib.Platform;
import jdk.test.lib.helpers.ClassFileInstaller;
import jdk.test.lib.process.OutputAnalyzer;

public class PrelinkedStringConcat extends ArchiveHeapTestClass {
    static final String bootJar = ClassFileInstaller.getJarPath("boot.jar");
    static final String appJar = ClassFileInstaller.getJarPath("app.jar");
    static final String[] appClassList = {"Hello"};

    static final String ConcatA_name = ConcatA.class.getName();

    public static void main(String[] args) throws Exception {
        if (!Platform.isDebugBuild()) {
          // FIXME throw skipped
          return;
        }

        OutputAnalyzer output;

        testCase("Simple positive case");
        output = dumpBootAndHello(ConcatA_name, "-Xlog:cds+heap+oops=trace:file=cds.oops.txt:none:filesize=0");
        mustSucceed(output);

        TestCommon.run("-Xbootclasspath/a:" + bootJar, "-cp", appJar, "-Xlog:cds+heap", ConcatA_name)
            .assertNormalExit("resolve subgraph " + ConcatA_name);
    }
}

class ConcatA {
    static Object[] archivedObjects;
    static {
        foo("000", "222");
        archivedObjects = new Object[0];
    }

    public static void main(String args[]) {
        foo("000", "222");
    }

    static String x;
    static void foo(String a, String b) {
        x = a + b;
    }
}
