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
 */

package jdk.test.lib.helpers;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.concurrent.TimeUnit;
import java.util.List;
import jdk.test.lib.JDKToolFinder;

/**
 * .jasm and .jcod files are usually compiled with the "@compile" tag in jtreg. However,
 * in some cases, a test may need to generate the .jasm and .jcod files dynamically.
 * ClassFileAssembler is used to compile these files into classes.
 *
 * The classes are written to the program's current directory.
 */

public class ClassFileAssembler {
    private static final String JAVA_PATH = JDKToolFinder.getJDKTool("java");
    private static final int COMPILE_TIMEOUT = 60;
    private static final float timeoutFactor = Float.parseFloat(System.getProperty("test.timeout.factor", "1.0"));

    public static void main(String... args) throws Exception {
        for (String file : args) {
            if (file.endsWith(".jasm")) {
                compileJasm(file);
            } else if (file.endsWith(".jcod")) {
                compileJcod(file);
            } else {
                throw new RuntimeException("Usage: jdk.test.lib.helpers.ClassFileAssembler <file> ...\n" +
                                           "where each file must be a .jasm or .jcod source file\n" +
                                           "The class files are written in " + System.getProperty("user.dir"));
            }
        }
    }

    /**
     * Compile the given .jasm source file to a class file in the current directory.
     */
    public static void compileJasm(String file) throws Exception {
        runTool("jasm", file);
    }

    /**
     * Compile the given .jcod source file to a class file in the current directory.
     */
    public static void compileJcod(String file) throws Exception {
        runTool("jcoder", file);
    }

    private static void runTool(String tool, String file) throws Exception {
        List<String> command = new ArrayList<>();
        command.add(JAVA_PATH);
        command.add("-classpath");
        command.add(getAsmToolsPath());
        command.add("org.openjdk.asmtools." + tool + ".Main");
        command.add("-d");
        command.add(System.getProperty("user.dir"));
        command.add(file);
        executeCompileCommand(command);
    }

    /**
     * Get the path of asmtools, which is shipped with JTREG.
     */
    private static String getAsmToolsPath() {
        for (String path : getClassPaths()) {
            if (path.endsWith("jtreg.jar")) {
                File jtreg = new File(path);
                File dir = jtreg.getAbsoluteFile().getParentFile();
                File asmtools = new File(dir, "asmtools.jar");
                if (!asmtools.exists()) {
                    throw new RuntimeException("Found jtreg.jar in classpath, but could not find asmtools.jar");
                }
                return asmtools.getAbsolutePath();
            }
        }
        throw new RuntimeException("Could not find asmtools because could not find jtreg.jar in classpath");
    }

    private static String[] getClassPaths() {
        String separator = File.pathSeparator;
        return System.getProperty("java.class.path").split(separator);
    }

    private static void executeCompileCommand(List<String> command) {
        ProcessBuilder builder = new ProcessBuilder(command);
        builder.redirectErrorStream(true);

        String output;
        int exitCode;
        try {
            Process process = builder.start();
            long timeout = COMPILE_TIMEOUT * (long)timeoutFactor;
            boolean exited = process.waitFor(timeout, TimeUnit.SECONDS);
            if (!exited) {
                process.destroyForcibly();
                System.out.println("Timeout: compile command: " + String.join(" ", command));
                throw new RuntimeException("Process timeout: compilation took too long.");
            }
            output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            exitCode = process.exitValue();
        } catch (IOException e) {
            throw new RuntimeException("IOException during compilation", e);
        } catch (InterruptedException e) {
            throw new RuntimeException("InterruptedException during compilation", e);
        }

        // Note: the output can be non-empty even if the compilation succeeds, e.g. for warnings.
        if (exitCode != 0) {
            System.err.println("Compilation failed.");
            System.err.println("Command: " + command);
            System.err.println("Exit code: " + exitCode);
            System.err.println("Output: '" + output + "'");
            throw new RuntimeException("Compilation failed.");
        }
    }
}
