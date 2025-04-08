/*
 * Copyright (c) 2017, 2024, Oracle and/or its affiliates. All rights reserved.
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

/*
 * @test id=aot
 * @key external-dep
 * @library /test/lib
 * @build jdk.test.whitebox.WhiteBox Scimark
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar CustomLauncher.jar CustomLauncher
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar WhiteBox.jar jdk.test.whitebox.WhiteBox
 * @run driver Scimark AOT
 */

/*
 * @test id=aot-cust
 * @key external-dep
 * @library /test/lib
 * @build jdk.test.whitebox.WhiteBox Scimark
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar CustomLauncher.jar CustomLauncher
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar WhiteBox.jar jdk.test.whitebox.WhiteBox
 * @run driver Scimark AOT custom-loader
 */

import java.io.File;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;
import java.util.Map;
import jdk.test.lib.artifacts.Artifact;
import jdk.test.lib.artifacts.ArtifactResolver;
import jdk.test.lib.artifacts.ArtifactResolverException;
import jdk.test.lib.cds.CDSAppTester;
import jdk.test.lib.cds.CDSJarUtils;
import jdk.test.lib.helpers.ClassFileInstaller;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.StringArrayUtils;
import jdk.test.whitebox.WhiteBox;

@Artifact(organization = "gov.nist.math", name = "scimark", revision = "2.0", extension = "zip")
public class Scimark {
    public static void main(String... args) throws Exception {
        Map<String, Path> artifacts;
        try {
            artifacts = ArtifactResolver.resolve(Scimark.class);
        } catch (ArtifactResolverException e) {
            throw new Error("TESTBUG: Can not resolve artifacts for "
                            + Scimark.class.getName(), e);
        }

        String scimarkCP = artifacts.get("gov.nist.math.scimark-2.0").toString();
        String scimarkJar = "scimark.jar";
        String launcherJar = "CustomLauncher.jar";

        CDSJarUtils.buildFromDirectory(scimarkJar, scimarkCP);

        String scimarkMain = "jnt.scimark2.commandline";

        // Run with a small <minimum_time> to limit total run time. See:
        // https://github.com/mork-optimization/scimark/blob/4cf2b7f2418f69e42725aa5a55115cbd6bf089c0/src/main/java/jnt/scimark2/CommandLine.java#L39
        String scimarkArg = "1";
        Tester tester;

        if (args.length > 1 && args[1].equals("custom-loader")) {
            tester = new Tester(launcherJar, "CustomLauncher", scimarkJar, scimarkMain, scimarkArg);
        } else {
            tester = new Tester(scimarkJar, scimarkMain, scimarkArg);
        }
        tester.run(args[0]);
    }

    static class Tester extends CDSAppTester {
        String appJar;
        String mainClass;
        String[] appArgs;
        public Tester(String appJar, String mainClass, String... appArgs) {
            super("Scimark-" + mainClass);
            this.appJar = appJar;
            this.mainClass = mainClass;
            this.appArgs = appArgs;
            useWhiteBox(ClassFileInstaller.getJarPath("WhiteBox.jar"));
        }

        @Override
        public String classpath(RunMode runMode) {
            return appJar;
        }

        @Override
        public String[] appCommandLine(RunMode runMode) {
            return StringArrayUtils.concat(mainClass, appArgs);
        }

        @Override
        public void checkExecution(OutputAnalyzer out, RunMode runMode) {

        }
    }
}

class CustomLauncher {
    public static void main(String args[]) throws Exception {
        WhiteBox wb = WhiteBox.getWhiteBox();
        String sciMarkJar = args[0];

        URL url = new File(sciMarkJar).toURI().toURL();
        URL[] urls = new URL[] {url};

        URLClassLoader urlClassLoader =
            new URLClassLoader("myloader", urls, CustomLauncher.class.getClassLoader());
        Class<?> mainClass = urlClassLoader.loadClass(args[1]);
        System.out.println(mainClass);
        mainClass.newInstance();

        if (wb.isSharedClass(CustomLauncher.class)) {
            // We are running with CustomLauncher from the AOT cache (or CDS achive)
            if (!wb.isSharedClass(mainClass)) {
                throw new RuntimeException(mainClass + " should be loaded from AOT cache (or CDS achive)");
            }
        }
    }
}
