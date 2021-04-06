/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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

package jdk.internal.reflect;

import java.lang.reflect.Method;
import java.util.Set;

import static jdk.internal.reflect.MethodHandleAccessorFactory.SPECIALIZED_PARAM_COUNT;

public class AccessorUtils {
    static void checkArgumentCount(int paramCount, Object[] args) {
        // only check argument count for specialized forms
        if (paramCount > SPECIALIZED_PARAM_COUNT) return;

        int argc = args != null ? args.length : 0;
        if (argc != paramCount) {
            throw new IllegalArgumentException("wrong number of arguments: " + argc + " expected: " + paramCount);
        }
    }

    static Object argAt(Object[] args, int index) {
        if (args != null && index < args.length) {
            return args[index];
        }
        return null;
    }

    static boolean isIllegalArgument(Class<?> implClass, RuntimeException e) {
        StackTraceElement[] stackTrace = e.getStackTrace();
        if (stackTrace.length == 0) {
            return false;       // would this happen?
        }

        int i = 0;
        StackTraceElement frame = stackTrace[0];
        if ((frame.getClassName().equals("java.lang.Class") && frame.getMethodName().equals("cast"))
                || (frame.getClassName().equals("java.util.Objects") && frame.getMethodName().equals("requiresNonNull"))) {
            // skip Class::cast and Objects::requireNonNull from top frame
            i++;
        }
        for (; i < stackTrace.length; i++) {
            frame = stackTrace[i];
            String cname = frame.getClassName();
            if (cname.equals(implClass.getName())) {
                // it's illegal argument if this exception is thrown from implClass
                return true;
            }
            if (frame.getModuleName() == null || !frame.getModuleName().equals("java.base")) {
                // if this exception is thrown from a unnamed module or non java.base module
                // it's not IAE as it's thrown from the reflective method
                return false;
            }
            // if thrown from java.base
            int index = cname.lastIndexOf(".");
            String pn = cname.substring(0, index);
            if (!IMPL_PACKAGES.contains(pn)) {
                return false;
            }
        }
        return false;
    }

    private static Set<String> IMPL_PACKAGES = Set.of(
            "java.lang.reflect",
            "java.lang.invoke",
            "jdk.internal.reflect",
            "sun.invoke.util"
    );

}
