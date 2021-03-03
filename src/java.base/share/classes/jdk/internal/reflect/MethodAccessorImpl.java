/*
 * Copyright (c) 2001, Oracle and/or its affiliates. All rights reserved.
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

import jdk.internal.access.SharedSecrets;

import java.lang.invoke.MethodHandle;
import java.lang.reflect.InvocationTargetException;

/** <P> Package-private implementation of the MethodAccessor interface
    which has access to all classes and all fields, regardless of
    language restrictions. See MagicAccessor. </P>

    <P> This class is known to the VM; do not change its name without
    also changing the VM's code. </P>

    <P> NOTE: ALL methods of subclasses are skipped during security
    walks up the stack. The assumption is that the only such methods
    that will persistently show up on the stack are the implementing
    methods for java.lang.reflect.Method.invoke(). </P>
*/

abstract class MethodAccessorImpl extends MagicAccessorImpl
    implements MethodAccessor {
    /** Matches specification in {@link java.lang.reflect.Method} */
    public abstract Object invoke(Object obj, Object[] args)
        throws IllegalArgumentException, InvocationTargetException;

    /**
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
    @Override
    public Object invoke(Class<?> caller, Object obj, Object[] args)
            throws IllegalArgumentException, InvocationTargetException {
        var invoker = SharedSecrets.getJavaLangInvokeAccess().reflectiveInvoker(caller);
        try {
            return invoker.invokeExact(methodAccessorInvoker(), obj, args);
        } catch (InvocationTargetException | RuntimeException | Error e) {
            throw e;
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    private MethodHandle maInvoker;
    private MethodHandle methodAccessorInvoker() {
        MethodHandle invoker = maInvoker;
        if (invoker == null) {
            maInvoker = invoker = MethodHandleAccessorFactory.METHOD_ACCESSOR_INVOKE.bindTo(this);
        }
        return invoker;
    }
}
