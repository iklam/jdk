/*
 * Copyright (c) 2001, 2021, Oracle and/or its affiliates. All rights reserved.
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

import java.lang.reflect.*;
import jdk.internal.misc.Unsafe;
import jdk.internal.misc.VM;
import sun.reflect.misc.ReflectUtil;

import static jdk.internal.reflect.ReflectionFactory.generateMethodAccessor;

/** Used only for the first few invocations of a Method; afterward,
    switches to bytecode-based implementation */

class NativeMethodAccessorImpl extends MethodAccessorImpl {
     private static final Unsafe U = Unsafe.getUnsafe();
     private static final long GENERATED_OFFSET
        = U.objectFieldOffset(NativeMethodAccessorImpl.class, "generated");

    private final Method method;
    private final Method csmAdapter;
    private final boolean callerSensitive;
    private DelegatingMethodAccessorImpl parent;
    private int numInvocations;
    private volatile int generated;
    NativeMethodAccessorImpl(Method method) {
        assert !Reflection.isCallerSensitive(method);
        this.method = method;
        this.csmAdapter = null;
        this.callerSensitive = false;
    }

    NativeMethodAccessorImpl(Method method, Method csmAdapter) {
        assert Reflection.isCallerSensitive(method);
        this.method = method;
        this.csmAdapter = csmAdapter;
        this.callerSensitive = true;
    }

    @Override
    public Object invoke(Object obj, Object[] args)
        throws IllegalArgumentException, InvocationTargetException
    {
        assert csmAdapter == null;

        maybeSwapDelegate(method);
        return invoke0(method, obj, args);
    }

    @Override
    public Object invoke(Class<?> caller, Object obj, Object[] args)
            throws IllegalArgumentException, InvocationTargetException
    {
        assert callerSensitive;

        if (csmAdapter != null) {
            maybeSwapDelegate(csmAdapter);

            Object[] newArgs = new Object[csmAdapter.getParameterCount()];
            newArgs[0] = caller;
            if (args != null) {
                System.arraycopy(args, 0, newArgs, 1, args.length);
            }
            return invoke0(csmAdapter, obj, newArgs);
        } else if (ReflectionFactory.useCallerSensitiveAdapter()) {
            // invoke the caller-sensitive method through an injected invoker,
            // a nestmate of the caller class, acting as the caller
            return super.invoke(caller, obj, args);
        } else {
            // ignore the caller sensitive adaptor for performance testing
            return invoke(obj, args);
        }
    }

    private void maybeSwapDelegate(Method method) {
        try {
            if (VM.isModuleSystemInited()
                    && ReflectionFactory.useDirectMethodHandle()
                    && generated == 0
                    && U.compareAndSetInt(this, GENERATED_OFFSET, 0, 1)) {
                MethodAccessorImpl acc = MethodHandleAccessorFactory.newMethodAccessor(method, callerSensitive);
                parent.setDelegate(acc);
            } else if (!ReflectionFactory.useDirectMethodHandle()
                    && ++numInvocations > ReflectionFactory.inflationThreshold()
                    && !method.getDeclaringClass().isHidden()
                    && !ReflectUtil.isVMAnonymousClass(method.getDeclaringClass())
                    && generated == 0
                    && U.compareAndSetInt(this, GENERATED_OFFSET, 0, 1)) {
                MethodAccessorImpl acc = generateMethodAccessor(method, csmAdapter);
                parent.setDelegate(acc);
            }
        } catch (Throwable t) {
            // Throwable happens in generateMethod, restore generated to 0
            generated = 0;
            throw t;
        }
    }

    void setParent(DelegatingMethodAccessorImpl parent) {
        this.parent = parent;
    }

    private static native Object invoke0(Method m, Object obj, Object[] args);
}
