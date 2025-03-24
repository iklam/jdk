/*
 * Copyright (c) 2014, 2024, Oracle and/or its affiliates. All rights reserved.
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
 * @test id=aot
 * @requires vm.cds.supports.aot.class.linking
 * @comment work around JDK-8345635
 * @requires !vm.jvmci.enabled
 * @library /test/lib /test/hotspot/jtreg/runtime/cds/appcds/test-classes/
 * @compile ../test-classes/Prohibited2.jasm
 * @build ProhibitedPackage
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar ProhibitedPackageApp.jar ProhibitedPackageApp Util java.foo.Prohibited2 Barbar
 * @run driver jdk.test.lib.helpers.ClassFileInstaller java/foo/Prohibited2
 * @run driver ProhibitedPackage AOT
 */

import java.lang.invoke.MethodHandles;
import java.io.File;
import jdk.test.lib.cds.CDSAppTester;
import jdk.test.lib.helpers.ClassFileInstaller;
import jdk.test.lib.process.OutputAnalyzer;

public class ProhibitedPackage {
    static final String appJar = ClassFileInstaller.getJarPath("ProhibitedPackageApp.jar");
    static final String mainClass = "ProhibitedPackageApp";

    public static void main(String args[]) throws Exception {
        (new Tester()).run(args);
    }

    static class Tester extends CDSAppTester {
        public Tester() {
            super(mainClass, mainClass, appJar);
        }

        @Override
        public void checkExecution(OutputAnalyzer out, RunMode runMode) throws Exception {
            //if (isAOTWorkflow() && runMode == RunMode.TRAINING) {
            //    out.shouldContain("Skipping BadOldClassA: Unlinked class not supported by AOTConfiguration");
            //}
        }
    }
}

class ProhibitedPackageApp {
    public static void main(String args[]) throws Exception {
        Class.forName("Barbar");

        new Barbar();
        //try {
            Class.forName("java.foo.Prohibited2");
        //}
        //try {
            Util.defineClass(MethodHandles.lookup(), new File("java/foo/Prohibited2.class"));
        //}
    }
}

interface Foofoo {}

class Barbar implements Foofoo {}
