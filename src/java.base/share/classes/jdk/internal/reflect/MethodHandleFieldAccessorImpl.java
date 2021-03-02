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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.WrongMethodTypeException;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;

abstract class MethodHandleFieldAccessorImpl extends FieldAccessorImpl {
    protected final MethodHandle getter;
    protected final MethodHandle setter;
    protected final boolean isReadOnly;
    protected final boolean isStatic;

    protected MethodHandleFieldAccessorImpl(Field field, MethodHandle getter, MethodHandle setter) {
        super(field);
        this.getter = getter;
        this.setter = setter;
        this.isReadOnly = setter == null;
        this.isStatic = Modifier.isStatic(field.getModifiers());
        }

    @Override
    public Object get(Object obj) throws IllegalArgumentException {
        ensureObj(obj);
        try {
            return getter.invoke(obj);
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (ClassCastException e) {
            throw new IllegalArgumentException("argument type mismatch", e);
        } catch (NullPointerException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    @Override
    public void set(Object obj, Object value) throws IllegalAccessException {
        ensureObj(obj);
        if (isReadOnly) {
            throwFinalFieldIllegalAccessException(value);
        }
        if (value == null) {
            throwSetIllegalArgumentException(value);
        }
        try {
            setter.invoke(obj, value);
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (ClassCastException e) {
            throw new IllegalArgumentException("argument type mismatch", e);
        } catch (NullPointerException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        } catch (WrongMethodTypeException e) {
            e.printStackTrace();
            throwSetIllegalArgumentException(value);
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    protected void ensureObj(Object o) {
        if (isStatic) return;

        // NOTE: will throw NullPointerException, as specified, if o is null
        if (!field.getDeclaringClass().isAssignableFrom(o.getClass())) {
            throwSetIllegalArgumentException(o);
        }
    }
}
