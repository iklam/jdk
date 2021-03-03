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
import java.lang.reflect.InvocationTargetException;

class DirectMethodAccessorImpl extends MethodAccessorImpl {
    static MethodAccessorImpl directMethodAccessor(MethodHandle dmh, boolean callerSensitive) {
        return callerSensitive ? new CallerSensitiveWithInvoker(dmh)
                               : new DirectMethodAccessorImpl(dmh);
    }
    static MethodAccessorImpl callerSensitiveAdapter(MethodHandle target) {
        return new CallerSensitiveWithLeadingCaller(target);
    }


    protected final MethodHandle target;      // target method handle
    private DirectMethodAccessorImpl(MethodHandle target) {
        this.target = target;
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

    static class CallerSensitiveWithLeadingCaller extends DirectMethodAccessorImpl {
        private CallerSensitiveWithLeadingCaller(MethodHandle target) {
            super(target);
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("cs$method invoked without explicit caller: " + target);
        }

        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws IllegalArgumentException, InvocationTargetException {
            try {
                return target.invokeExact(obj, caller, args);
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
    }

    static class CallerSensitiveWithInvoker extends DirectMethodAccessorImpl {
        private static final JavaLangInvokeAccess JLIA = SharedSecrets.getJavaLangInvokeAccess();

        private CallerSensitiveWithInvoker(MethodHandle target) {
            super(target);
        }

        @Override
        public Object invoke(Object obj, Object[] args) throws InvocationTargetException {
            throw new InternalError("caller-sensitive method invoked without explicit caller: " + target);
        }

        @Override
        @ForceInline
        public Object invoke(Class<?> caller, Object obj, Object[] args) throws IllegalArgumentException, InvocationTargetException {
            var invoker = JLIA.reflectiveInvoker(caller);
            try {
                return invoker.invokeExact(target, obj, args);
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
    }
}

