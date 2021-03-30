/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
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

package org.openjdk.bench.java.lang.reflect;

import org.openjdk.jmh.annotations.Benchmark;
import org.openjdk.jmh.annotations.BenchmarkMode;
import org.openjdk.jmh.annotations.Fork;
import org.openjdk.jmh.annotations.Level;
import org.openjdk.jmh.annotations.Measurement;
import org.openjdk.jmh.annotations.Mode;
import org.openjdk.jmh.annotations.OutputTimeUnit;
import org.openjdk.jmh.annotations.Scope;
import org.openjdk.jmh.annotations.Setup;
import org.openjdk.jmh.annotations.State;
import org.openjdk.jmh.annotations.Warmup;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.TimeUnit;

@BenchmarkMode(Mode.AverageTime)
@Warmup(iterations = 5, time = 1)
@Measurement(iterations = 10, time = 1)
@OutputTimeUnit(TimeUnit.NANOSECONDS)
@State(Scope.Thread)
@Fork(value = 1, warmups = 0)
public class ReflectionSpeedBenchmark {
    static final Method staticMethodConst;
    static final Method instanceMethodConts;
    static final Method classForName1argConst;
    static final Method classForName3argConst;
    static final Field staticFieldConst;
    static final Field instanceFieldConst;
    static final Constructor<?> ctorConst;

    static Method staticMethodVar;
    static Method instanceMethodVar;
    static Method classForName1argVar;
    static Method classForName3argVar;
    static Field staticFieldVar;
    static Field instanceFieldVar;
    static Constructor<?> ctorVar;

    static {
        try {
            staticMethodVar = staticMethodConst = ReflectionSpeedBenchmark.class.getDeclaredMethod("sumStatic", int.class, int.class);
            instanceMethodVar = instanceMethodConts = ReflectionSpeedBenchmark.class.getDeclaredMethod("sumInstance", int.class, int.class);
            classForName1argVar = classForName1argConst = Class.class.getMethod("forName", String.class);
            classForName3argVar = classForName3argConst = Class.class.getMethod("forName", String.class, boolean.class, ClassLoader.class);
            staticFieldVar = staticFieldConst = ReflectionSpeedBenchmark.class.getDeclaredField("staticFoo");
            instanceFieldVar = instanceFieldConst = ReflectionSpeedBenchmark.class.getDeclaredField("foo");
            ctorVar = ctorConst = Foo.class.getDeclaredConstructor();
        } catch (NoSuchMethodException|NoSuchFieldException e) {
            throw new NoSuchMethodError(e.getMessage());
        }
    }

    public static int staticFoo;
    public int foo;
    private int a, b;
    static class Foo {
        public Foo() {};
    }

    @Setup(Level.Iteration)
    public void setup() {
        a = ThreadLocalRandom.current().nextInt(1024, Integer.MAX_VALUE);
        b = ThreadLocalRandom.current().nextInt(1024, Integer.MAX_VALUE);
    }

    public static int sumStatic(int a, int b) {
        return a + b;
    }

    public int sumInstance(int a, int b) {
        return a + b;
    }

    @Benchmark
    public Foo newInstance() {
        return new Foo();
    }

    @Benchmark
    public int staticDirect() {
        return sumStatic(a, b);
    }

    @Benchmark
    public int instanceDirect() {
        return sumInstance(a, b);
    }

    @Benchmark
    public int staticGetFieldDirect() {
        return staticFoo;
    }
    @Benchmark
    public int staticPutFieldDirect() {
        return staticFoo = 10;
    }

    @Benchmark
    public int getFieldDirect() {
        return foo;
    }
    @Benchmark
    public int putFieldDirect() {
        return foo = 10;
    }

    @Benchmark
    public int staticReflectiveConst() {
        try {
            return (int) staticMethodConst.invoke(null, a, b);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int instanceReflectiveConst() {
        try {
            return (int) instanceMethodConts.invoke(this, a, b);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int staticReflectiveGetterConst() {
        try {
            return (int) staticFieldConst.get(null);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int instanceReflectiveGetterConst() {
        try {
            return (int) instanceFieldConst.get(this);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public void staticReflectiveSetterConst() {
        try {
            staticFieldConst.setInt(null, 10);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public void instanceReflectiveSetterConst() {
        try {
            instanceFieldConst.setInt(this, 20);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public Class<?> classForName1argConst() {
        try {
            return (Class<?>) classForName1argVar.invoke(null, "java.lang.System");
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }
    @Benchmark
    public Class<?> classForName3argConst() {
        try {
            return (Class<?>) classForName3argVar.invoke(null, "java.lang.System", false, null);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }
    @Benchmark
    public int staticReflectiveVar() {
        try {
            return (int) staticMethodVar.invoke(null, a, b);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int instanceReflectiveVar() {
        try {
            return (int) instanceMethodVar.invoke(this, a, b);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int staticReflectiveGetterVar() {
        try {
            return (int) staticFieldVar.get(null);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public int instanceReflectiveGetterVar() {
        try {
            return (int) instanceFieldVar.get(this);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public void staticReflectiveSetterVar() {
        try {
            staticFieldVar.setInt(null, 10);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public void instanceReflectiveSetterVar() {
        try {
            instanceFieldVar.setInt(this, 20);
        } catch (IllegalAccessException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public Class<?> classForName1argVar() {
        try {
            return (Class<?>) classForName1argVar.invoke(null, "java.lang.System");
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }

    @Benchmark
    public Class<?> classForName3argVar() {
        try {
            return (Class<?>) classForName3argVar.invoke(null, "java.lang.System", false, null);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new AssertionError(e);
        }
    }
}
