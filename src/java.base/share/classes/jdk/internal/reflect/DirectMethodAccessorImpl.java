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

import jdk.internal.vm.annotation.ForceInline;

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.ref.WeakReference;
import java.lang.reflect.InvocationTargetException;

class DirectMethodAccessorImpl extends MethodAccessorImpl {
    protected final MethodHandle target;      // target method handle
    protected final int modifiers;

    DirectMethodAccessorImpl(MethodHandle target, int modifiers) {
        this.target = target;
        this.modifiers = modifiers;
    }

    @Override
    @ForceInline
    public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
        try {
            return target.invokeExact(obj, args);
        } catch (IllegalArgumentException | InvocationTargetException e) {
            throw e;
        } catch (ClassCastException e) {
            throw new IllegalArgumentException("argument type mismatch", e);
        } catch (NullPointerException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        } catch (Error|RuntimeException e) {
            throw e;
        } catch (Throwable e) {
            throw new InvocationTargetException(e);
        }
    }

    static class CallerSensitiveMethodAccessorImpl extends DirectMethodAccessorImpl {
        private volatile Cache csmCache;
        private final boolean hasTrailingCallerArgument;
        CallerSensitiveMethodAccessorImpl(MethodHandle original, int modifiers, boolean hasTrailingCallerArgument) {
            super(original, modifiers);
            this.hasTrailingCallerArgument = hasTrailingCallerArgument;
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("caller-sensitive method: " + target);
        }

        /*
         * This method invokes the method handle of a caller-sensitive method.
         * The caller parameter is the caller class invoking Method::invoke.
         *
         * This method creates a new MH for each different caller class.
         * The target MH is bound with the caller class and an invoker class
         * is injected such that the invoker class will be the caller class
         * of the CSM via MH.  The injected invoker class is in the same runtime
         * package, has the same defining class loader and protection domain
         * as the caller parameter.
         *
         * CSM.invoke does stack walking once to find the caller class
         * in this implementation.  The invocation of the CSM may also call
         * Reflection::getCallerClass another time.
         *
         * If a caller-sensitive method implements an alternative method
         * with "reflected$" prefix that takes an additional trailing caller
         * class argument, such alternative implementation will be invoked instead.
         */
        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args)
                throws IllegalArgumentException, InvocationTargetException {
            try {
                MethodHandle dmh = bindCaller(target, caller);
                return dmh.invokeExact(obj, args);
            } catch (IllegalArgumentException | InvocationTargetException e) {
                throw e;
            } catch (ClassCastException e) {
                throw new IllegalArgumentException("argument type mismatch", e);
            } catch (NullPointerException e) {
                throw new IllegalArgumentException(e.getMessage(), e);
            } catch (Error|RuntimeException e) {
                throw e;
            } catch (Throwable e) {
                throw new InvocationTargetException(e);
            }
        }

        private MethodHandle bindCaller(MethodHandle original, Class<?> caller) {
            // direct method handle to the caller-sensitive method invoked by the given caller
            MethodHandle dmh = csmCache != null ? csmCache.methodHandle(caller) : null;
            if (dmh == null) {
                try {
                    if (hasTrailingCallerArgument) {
                        dmh = MethodHandles.insertArguments(original, original.type().parameterCount() - 1, caller);
                    } else {
                        dmh = MethodHandleAccessorFactory.rebindCaller(caller, original);
                    }
                    dmh = MethodHandleAccessorFactory.makeTarget(dmh, modifiers);

                    // push MH into cache
                    csmCache = new Cache(caller, dmh);
                } catch (IllegalAccessException e) {
                    throw new InternalError(e);
                }
            }
            return dmh;

        }

        static class Cache {
            final WeakReference<Class<?>> callerRef;
            final WeakReference<MethodHandle> targetRef;

            Cache(Class<?> caller, MethodHandle target) {
                this.callerRef = new WeakReference<>(caller);
                this.targetRef = new WeakReference<>(target);
            }

            MethodHandle methodHandle(Class<?> caller) {
                if (callerRef.refersTo(caller)) {
                    return targetRef.get();
                }
                return null;
            }
        }
    }
}
