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

import jdk.internal.access.JavaLangInvokeAccess;
import jdk.internal.access.SharedSecrets;
import jdk.internal.vm.annotation.ForceInline;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.WrongMethodTypeException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Set;

class DirectMethodAccessorImpl extends MethodAccessorImpl {
    static MethodAccessorImpl directMethodAccessor(Method method, MethodHandle dmh, boolean callerSensitive) {
        return callerSensitive ? new CallerSensitiveWithInvoker(dmh, method)
                               : new DirectMethodAccessorImpl(dmh, method);
    }
    static MethodAccessorImpl callerSensitiveAdapter(Method original, MethodHandle target) {
        return new CallerSensitiveWithLeadingCaller(target, original);
    }

    protected final Method method;
    protected final MethodHandle target;      // target method handle
    protected final boolean isStatic;

    protected final int paramCount;
    private DirectMethodAccessorImpl(MethodHandle target, Method method) {
        this.target = target;
        this.method = method;
        this.isStatic = Modifier.isStatic(method.getModifiers());
        this.paramCount = method.getParameterCount();
    }

    @Override
    @ForceInline
    public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
        if (MethodHandleAccessorFactory.VERBOSE) {
            System.out.println("invoke " + method + " with target " + obj + " args " + Arrays.deepToString(args));
        }
        checkReceiver(obj);
        checkArgumentCount(args);
        try {
            return switch (paramCount) {
                case 0 ->  isStatic ? target.invokeExact()
                                    : target.invokeExact(obj);
                case 1 ->  isStatic ? target.invokeExact(argAt(args, 0))
                                    : target.invokeExact(obj, argAt(args, 0));
                case 2 ->  isStatic ? target.invokeExact(argAt(args, 0), argAt(args, 1))
                                    : target.invokeExact(obj, argAt(args, 0), argAt(args, 1));
                default -> isStatic ? target.invokeExact(args)
                                    : target.invokeExact(obj, args);
            };
        } catch (ClassCastException|WrongMethodTypeException e) {
            if (isIllegalArgument(e))
                throw new IllegalArgumentException("argument type mismatch", e);
            else
                throw new InvocationTargetException(e);
        } catch (NullPointerException e) {
            if (isIllegalArgument(e))
                throw new IllegalArgumentException(e);
            else
                throw new InvocationTargetException(e);
        } catch (Throwable e) {
            throw new InvocationTargetException(e);
        }
    }

    void checkReceiver(Object obj) {
        if (!isStatic && obj == null) {
            throw new NullPointerException();
        }
    }

    void checkArgumentCount(Object[] args) {
        // only check argument count for specialized forms
        if (paramCount > 2) return;

        int argc = args != null ? args.length : 0;
        if (argc != paramCount) {
            throw new IllegalArgumentException("wrong number of arguments");
        }
    }

    private static Object argAt(Object[] args, int index) {
        if (args != null && index < args.length) {
            return args[index];
        }
        return null;
    }

    boolean isIllegalArgument(RuntimeException e) {
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
            if (cname.equals(this.getClass().getName())) {
                // it's illegal argument if this exception is thrown from
                // DirectMethodAccessorImpl::invoke
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

    static class CallerSensitiveWithLeadingCaller extends DirectMethodAccessorImpl {
        private CallerSensitiveWithLeadingCaller(MethodHandle target, Method original) {
            super(target, original);
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("caller sensitive method invoked without explicit caller: " + target);
        }

        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws InvocationTargetException {
            if (MethodHandleAccessorFactory.VERBOSE) {
                System.out.println("caller " + caller.getName() + " target " + obj + " args " + Arrays.deepToString(args));
            }
            checkReceiver(obj);
            checkArgumentCount(args);
            try {
                return switch (paramCount) {
                    case 0 ->  isStatic ? target.invokeExact(caller)
                                        : target.invokeExact(obj, caller);
                    case 1 ->  isStatic ? target.invokeExact(caller, argAt(args, 0))
                                        : target.invokeExact(obj, caller, argAt(args, 0));
                    case 2 ->  isStatic ? target.invokeExact(caller, argAt(args, 0), argAt(args, 1))
                                        : target.invokeExact(obj, caller, argAt(args, 0), argAt(args, 1));
                    default -> isStatic ? target.invokeExact(caller, args)
                                        : target.invokeExact(obj, caller, args);
                };
            } catch (ClassCastException | WrongMethodTypeException e) {
                if (isIllegalArgument(e))
                    throw new IllegalArgumentException("argument type mismatch", e);
                else
                    throw new InvocationTargetException(e);
            } catch (NullPointerException e) {
                if (isIllegalArgument(e))
                    throw new IllegalArgumentException(e);
                else
                    throw new InvocationTargetException(e);
            } catch (Throwable e) {
                throw new InvocationTargetException(e);
            }
        }
    }

    /**
     * MethodAccessor class to invoke caller-sensitive methods via a reflective invoker
     * injected as a caller class to invoke a method handle.
     *
     * This uses the simple form of direct method handle with the same method type
     * as Method::invoke.
     *
     * To use specialized target method handles (see MethodHandleAccessorFactory::makeSpecializedTarget)
     * it needs support in the injected invoker::reflect_invoke_V for different specialized forms.
     */
    static class CallerSensitiveWithInvoker extends DirectMethodAccessorImpl {
        private static final JavaLangInvokeAccess JLIA = SharedSecrets.getJavaLangInvokeAccess();

        private CallerSensitiveWithInvoker(MethodHandle target, Method method) {
            super(target, method);
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("caller-sensitive method invoked without explicit caller: " + target);
        }

        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws InvocationTargetException {
            if (MethodHandleAccessorFactory.VERBOSE) {
                System.out.println("target " + obj + " args " + Arrays.deepToString(args));
            }
            checkReceiver(obj);
            checkArgumentCount(args);
            var invoker = JLIA.reflectiveInvoker(caller);
            try {
                // invoke the target method handle via an invoker
                return invoker.invokeExact(target, obj, args);
            } catch (ClassCastException|WrongMethodTypeException e) {
                if (isIllegalArgument(e))
                    throw new IllegalArgumentException("argument type mismatch", e);
                else
                    throw new InvocationTargetException(e);
            } catch (NullPointerException e) {
                if (isIllegalArgument(e))
                    throw new IllegalArgumentException(e);
                else
                    throw new InvocationTargetException(e);
            } catch (Throwable e) {
                throw new InvocationTargetException(e);
            }
        }
    }
}

