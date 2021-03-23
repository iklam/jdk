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

import java.lang.invoke.VarHandle;
import java.lang.invoke.WrongMethodTypeException;
import java.lang.reflect.Field;

class VarHandleIntegerFieldAccessorImpl extends VarHandleFieldAccessorImpl {
    VarHandleIntegerFieldAccessorImpl(Field field, VarHandle varHandle, boolean isReadyOnly) {
        super(field, varHandle, isReadyOnly);
    }
    public Object get(Object obj) throws IllegalArgumentException {
        return Integer.valueOf(getInt(obj));
    }

    public boolean getBoolean(Object obj) throws IllegalArgumentException {
        throw newGetBooleanIllegalArgumentException();
    }

    public byte getByte(Object obj) throws IllegalArgumentException {
        throw newGetByteIllegalArgumentException();
    }

    public char getChar(Object obj) throws IllegalArgumentException {
        throw newGetCharIllegalArgumentException();
    }

    public short getShort(Object obj) throws IllegalArgumentException {
        throw newGetShortIllegalArgumentException();
    }

    @ForceInline
    public int getInt(Object obj) throws IllegalArgumentException {
        try {
            return isStatic ? (int) varHandle.get() : (int) varHandle.get(obj);
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (ClassCastException e) {
            throw newIllegalArgumentException(obj);
        } catch (NullPointerException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    public long getLong(Object obj) throws IllegalArgumentException {
        return getInt(obj);
    }

    public float getFloat(Object obj) throws IllegalArgumentException {
        return getInt(obj);
    }

    public double getDouble(Object obj) throws IllegalArgumentException {
        return getInt(obj);
    }

    public void set(Object obj, Object value)
            throws IllegalArgumentException, IllegalAccessException
    {
        if (isReadOnly) {
            throwFinalFieldIllegalAccessException(value);
        }
        if (value == null) {
            throwSetIllegalArgumentException(value);
        }
        if (value instanceof Byte) {
            setInt(obj, ((Byte) value).byteValue());
            return;
        }
        if (value instanceof Short) {
            setInt(obj, ((Short) value).shortValue());
            return;
        }
        if (value instanceof Character) {
            setInt(obj, ((Character) value).charValue());
            return;
        }
        if (value instanceof Integer) {
            setInt(obj, ((Integer) value).intValue());
            return;
        }
        throwSetIllegalArgumentException(value);
    }

    public void setBoolean(Object obj, boolean z)
        throws IllegalArgumentException, IllegalAccessException
    {
        throwSetIllegalArgumentException(z);
    }

    public void setByte(Object obj, byte b)
        throws IllegalArgumentException, IllegalAccessException
    {
        setInt(obj, b);
    }

    public void setChar(Object obj, char c)
        throws IllegalArgumentException, IllegalAccessException
    {
        setInt(obj, c);
    }

    public void setShort(Object obj, short s)
        throws IllegalArgumentException, IllegalAccessException
    {
        setInt(obj, s);
    }

    public void setInt(Object obj, int i)
        throws IllegalArgumentException, IllegalAccessException
    {
        if (isReadOnly) {
            throwFinalFieldIllegalAccessException(i);
        }
        try {
            if (isStatic) {
                varHandle.set(i);
            } else {
                varHandle.set(obj, i);
            }
        } catch (IllegalArgumentException e) {
            throw e;
        } catch (ClassCastException e) {
            throw newIllegalArgumentException(obj);
        } catch (NullPointerException e) {
            throw new IllegalArgumentException(e.getMessage(), e);
        } catch (WrongMethodTypeException e) {
            throwSetIllegalArgumentException(i);
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    public void setLong(Object obj, long l)
        throws IllegalArgumentException, IllegalAccessException
    {
        throwSetIllegalArgumentException(l);
    }

    public void setFloat(Object obj, float f)
        throws IllegalArgumentException, IllegalAccessException
    {
        throwSetIllegalArgumentException(f);
    }

    public void setDouble(Object obj, double d)
        throws IllegalArgumentException, IllegalAccessException
    {
        throwSetIllegalArgumentException(d);
    }
}
