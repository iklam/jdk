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
import jdk.internal.misc.VM;
import jdk.internal.vm.annotation.ForceInline;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.WrongMethodTypeException;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.Objects;

import static jdk.internal.reflect.AccessorUtils.argAt;
import static jdk.internal.reflect.AccessorUtils.checkArgumentCount;
import static jdk.internal.reflect.AccessorUtils.isIllegalArgument;
import static jdk.internal.reflect.MethodHandleAccessorFactory.findCSMethodAdapter;

class DirectMethodAccessorImpl extends MethodAccessorImpl {
    static MethodAccessorImpl methodAccessor(Method method, MethodHandle target, MHMethodAccessor mhInvoker) {
        return new DirectMethodAccessorImpl(method, mhInvoker);
    }

    static MethodAccessorImpl callerSensitiveMethodAccessor(Method method, MethodHandle dmh) {
        return new CallerSensitiveWithInvoker(dmh, method);
    }

    /**
     * Target method handle is the adapter method with the leading caller class
     * for the given original method.
     */
    static MethodAccessorImpl callerSensitiveAdapter(Method original, MethodHandle target, MHMethodAccessor mhInvoker) {
        return new CallerSensitiveWithLeadingCaller(original, mhInvoker);
    }

    static MethodAccessorImpl nativeAccessor(Method method, boolean callerSensitive) {
        assert !VM.isJavaLangInvokeInited() || Modifier.isNative(method.getModifiers());
        return callerSensitive ? new NativeAccessor(method, findCSMethodAdapter(method))
                               : new NativeAccessor(method);
    }

    protected final Method method;
    protected final MHMethodAccessor mhInvoker;
    protected final boolean isStatic;
    protected final int paramCount;
    private DirectMethodAccessorImpl(Method method, MHMethodAccessor mhInvoker) {
        this.method = method;
        this.mhInvoker = mhInvoker;
        this.isStatic = Modifier.isStatic(method.getModifiers());
        this.paramCount = method.getParameterCount();
    }

    @Override
    @ForceInline
    public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
        if (MethodHandleAccessorFactory.VERBOSE) {
            System.out.println("invoke " + method.getDeclaringClass().getName() + "::" + method.getName()
                    + " with target " + obj + " args " + Arrays.deepToString(args));
        }
        if (!isStatic) {
            Objects.requireNonNull(obj);
        }
        checkArgumentCount(paramCount, args);
        try {
            return switch (paramCount) {
                case 0 ->  isStatic ? mhInvoker.invoke()
                                    : mhInvoker.invoke(obj);
                case 1 ->  isStatic ? mhInvoker.invoke(argAt(args, 0))
                                    : mhInvoker.invoke(obj, argAt(args, 0));
                case 2 ->  isStatic ? mhInvoker.invoke(argAt(args, 0), argAt(args, 1))
                                    : mhInvoker.invoke(obj, argAt(args, 0), argAt(args, 1));
                default -> isStatic ? mhInvoker.invoke(args)
                                    : mhInvoker.invoke(obj, args);
            };
        } catch (ClassCastException|WrongMethodTypeException e) {
            if (isIllegalArgument(this.getClass(), e))
                throw new IllegalArgumentException("argument type mismatch", e);
            else
                throw new InvocationTargetException(e);
        } catch (NullPointerException e) {
            if (isIllegalArgument(this.getClass(), e))
                throw new IllegalArgumentException(e);
            else
                throw new InvocationTargetException(e);
        } catch (Throwable e) {
            throw new InvocationTargetException(e);
        }
    }


    static class CallerSensitiveWithLeadingCaller extends DirectMethodAccessorImpl {
        private CallerSensitiveWithLeadingCaller(Method original, MHMethodAccessor mhInvoker) {
            super(original, mhInvoker);
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("caller sensitive method invoked without explicit caller: " + method);
        }

        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws InvocationTargetException {
            if (MethodHandleAccessorFactory.VERBOSE) {
                System.out.println("caller " + caller.getName() + " target " + obj + " args " + Arrays.deepToString(args));
            }
            if (!isStatic) {
                Objects.requireNonNull(obj);
            }
            checkArgumentCount(paramCount, args);
            try {
                return switch (paramCount) {
                    case 0 ->  isStatic ? mhInvoker.invoke(caller)
                                        : mhInvoker.invoke(obj, caller);
                    case 1 ->  isStatic ? mhInvoker.invoke(caller, argAt(args, 0))
                                        : mhInvoker.invoke(obj, caller, argAt(args, 0));
                    case 2 ->  isStatic ? mhInvoker.invoke(caller, argAt(args, 0), argAt(args, 1))
                                        : mhInvoker.invoke(obj, caller, argAt(args, 0), argAt(args, 1));
                    default -> isStatic ? mhInvoker.invoke(caller, args)
                                        : mhInvoker.invoke(obj, caller, args);
                };
            } catch (ClassCastException|WrongMethodTypeException e) {
                if (isIllegalArgument(this.getClass(), e))
                    throw new IllegalArgumentException("argument type mismatch", e);
                else
                    throw new InvocationTargetException(e);
            } catch (NullPointerException e) {
                if (isIllegalArgument(this.getClass(), e))
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
        private final MethodHandle target;      // target method handle
        private CallerSensitiveWithInvoker(MethodHandle target, Method method) {
            super(method, null);
            this.target = target;
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
            if (!isStatic) {
                Objects.requireNonNull(obj);
            }
            checkArgumentCount(paramCount, args);
            var invoker = JLIA.reflectiveInvoker(caller);
            try {
                // invoke the target method handle via an invoker
                return invoker.invokeExact(target, obj, args);
            } catch (ClassCastException|WrongMethodTypeException e) {
                if (isIllegalArgument(this.getClass(), e))
                    throw new IllegalArgumentException("argument type mismatch", e);
                else
                    throw new InvocationTargetException(e);
            } catch (NullPointerException e) {
                if (isIllegalArgument(this.getClass(), e))
                    throw new IllegalArgumentException(e);
                else
                    throw new InvocationTargetException(e);
            } catch (Throwable e) {
                throw new InvocationTargetException(e);
            }
        }
    }

    /**
     * Invoke the method via native VM reflection
     */
    static class NativeAccessor extends MethodAccessorImpl {
        private final Method method;
        private final Method csmAdapter;
        private final boolean callerSensitive;
        NativeAccessor(Method method) {
            assert !Reflection.isCallerSensitive(method);
            this.method = method;
            this.csmAdapter = null;
            this.callerSensitive = false;
        }

        NativeAccessor(Method method, Method csmAdapter) {
            assert Reflection.isCallerSensitive(method);
            this.method = method;
            this.csmAdapter = csmAdapter;
            this.callerSensitive = true;
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            assert csmAdapter == null;
            return invoke0(method, obj, args);
        }

        @Override
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws InvocationTargetException {
            assert callerSensitive;

            if (csmAdapter != null) {
                Object[] newArgs = new Object[csmAdapter.getParameterCount()];
                newArgs[0] = caller;
                if (args != null) {
                    System.arraycopy(args, 0, newArgs, 1, args.length);
                }
                return invoke0(csmAdapter, obj, newArgs);
            } else {
                /*
                 * When Method::invoke on a caller-sensitive method is to be invoked
                 * and no adapter method with a leading caller class argument is defined,
                 * the caller-sensitive method must be invoked via an invoker injected
                 * which has the following signature:
                 *     reflect_invoke_V(MethodHandle mh, Object target, Object[] args)
                 *
                 * The stack frames calling the method `csm` through reflection will
                 * look like this:
                 *     target.csm(args)
                 *     NativeMethodAccesssorImpl::invoke(target, args)
                 *     MethodAccessImpl::invoke(target, args)
                 *     InjectedInvoker::reflect_invoke_V(vamh, target, args);
                 *     method::invoke(target, args)
                 *     p.Foo::m
                 *
                 * An injected invoker class is a hidden class which has the same
                 * defining class loader, runtime package, and protection domain
                 * as the given caller class.
                 *
                 * This method is needed by NativeMethodAccessorImpl and the generated
                 * MethodAccessor.   The caller-sensitive method will call
                 * Reflection::getCallerClass to get the caller class.
                 */
                var invoker = SharedSecrets.getJavaLangInvokeAccess().reflectiveInvoker(caller);
                try {
                    return invoker.invokeExact(methodAccessorInvoker(), obj, args);
                } catch (InvocationTargetException|RuntimeException|Error e) {
                    throw e;
                } catch (Throwable e) {
                    throw new InternalError(e);
                }
            }
        }

        private MethodHandle maInvoker;
        private MethodHandle methodAccessorInvoker() {
            MethodHandle invoker = maInvoker;
            if (invoker == null) {
                maInvoker = invoker = MethodHandleAccessorFactory.reflectiveInvokerFor(this);
            }
            return invoker;
        }

        private static native Object invoke0(Method m, Object obj, Object[] args);
    }
}

