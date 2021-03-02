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

/*
 * @test
 * @bug 6824466
 * @modules java.base/jdk.internal.reflect
 * @summary Test compliance of ConstructorAccessor and MethodAccessor implementations
 * @run testng/othervm --add-exports java.base/jdk.internal.reflect=ALL-UNNAMED -Djdk.reflect.useDirectMethodHandle=true -XX:-ShowCodeDetailsInExceptionMessages MethodHandleAccessorsTest
 * @run testng/othervm --add-exports java.base/jdk.internal.reflect=ALL-UNNAMED -Djdk.reflect.useDirectMethodHandle=false -XX:-ShowCodeDetailsInExceptionMessages MethodHandleAccessorsTest
 */

import jdk.internal.reflect.ConstructorAccessor;
import jdk.internal.reflect.MethodAccessor;
import jdk.internal.reflect.ReflectionFactory;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Objects;
import java.util.function.IntUnaryOperator;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.testng.annotations.DataProvider;
import org.testng.annotations.Test;

public class MethodHandleAccessorsTest {

    public static void public_static_V() {}

    public static int public_static_I() { return 42; }

    public void public_V() {}

    public int public_I() { return 42; }

    public static void public_static_I_V(int i) {}

    public static int public_static_I_I(int i) { return i; }

    public void public_I_V(int i) {}

    public int public_I_I(int i) { return i; }

    private static void private_static_V() {}

    private static int private_static_I() { return 42; }

    private void private_V() {}

    private int private_I() { return 42; }

    private static void private_static_I_V(int i) {}

    private static int private_static_I_I(int i) { return i; }

    private void private_I_V(int i) {}

    private int private_I_I(int i) { return i; }

    public static int varargs_primitive(int first, int... rest) {
        int sum = first;
        if (rest != null) {
            sum *= 100;
            for (int i : rest) sum += i;
        }
        return sum;
    }

    public static String varargs_object(String first, String... rest) {
        StringBuilder sb = new StringBuilder(first);
        if (rest != null) {
            sb.append(Stream.of(rest).collect(Collectors.joining(",", "[", "]")));
        }
        return sb.toString();
    }

    public static void throws_exception(RuntimeException exc) {
        throw exc;
    }

    public static final class Public {
        private final int i;
        private final String s;

        public Public() {
            this.i = 0;
            this.s = null;
        }

        public Public(int i) {
            this.i = i;
            this.s = null;
        }

        public Public(String s) {
            this.i = 0;
            this.s = s;
        }

        public Public(int first, int... rest) {
            this(varargs_primitive(first, rest));
        }

        public Public(String first, String... rest) {
            this(varargs_object(first, rest));
        }

        public Public(RuntimeException exc) {
            throw exc;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;
            Public other = (Public) o;
            return i == other.i &&
                   Objects.equals(s, other.s);
        }

        @Override
        public int hashCode() {
            return Objects.hash(i, s);
        }

        @Override
        public String toString() {
            return "Public{" +
                   "i=" + i +
                   ", s='" + s + '\'' +
                   '}';
        }
    }

    static final class Private {
        private final int i;

        private Private() {
            this.i = 0;
        }

        private Private(int i) {
            this.i = i;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) return true;
            if (o == null || getClass() != o.getClass()) return false;
            Private other = (Private) o;
            return i == other.i;
        }

        @Override
        public int hashCode() {
            return Objects.hash(i);
        }

        @Override
        public String toString() {
            return "Private{" +
                   "i=" + i +
                   '}';
        }
    }

    public static abstract class Abstract {
        public Abstract() {
        }
    }

    static void doTestAccessor(Method m, MethodAccessor ma, Object target, Object[] args, Object expectedReturn, Throwable... expectedExceptions) {
        Object ret;
        Throwable exc;
        try {
            ret = ma.invoke(target, args);
            exc = null;
        } catch (Throwable e) {
            ret = null;
            exc = e;
        }
        System.out.println("\n" + m + ", invoked with target: " + target + ", args: " + Arrays.toString(args));

        chechResult(ret, expectedReturn, exc, expectedExceptions);
    }

    static void doTestAccessor(Constructor c, ConstructorAccessor ca, Object[] args, Object expectedReturn, Throwable... expectedExceptions) {
        Object ret;
        Throwable exc;
        try {
            ret = ca.newInstance(args);
            exc = null;
        } catch (Throwable e) {
            ret = null;
            exc = e;
        }
        System.out.println("\n" + c + ", invoked with args: " + Arrays.toString(args));

        chechResult(ret, expectedReturn, exc, expectedExceptions);
    }

    static void chechResult(Object ret, Object expectedReturn, Throwable exc, Throwable... expectedExceptions) {
        if (exc != null) {
            checkException(exc, expectedExceptions);
        } else if (expectedExceptions.length > 0) {
            fail(exc, expectedExceptions);
        } else if (!Objects.equals(ret, expectedReturn)) {
            throw new AssertionError("Expected return:\n " + expectedReturn + "\ngot:\n " + ret);
        } else {
            System.out.println("    Got expected return: " + ret);
        }
    }

    static void checkException(Throwable exc, Throwable... expectedExceptions) {
        boolean match = false;
        for (Throwable expected : expectedExceptions) {
            if (exceptionMatches(exc, expected)) {
                match = true;
                break;
            }
        }
        if (match) {
            System.out.println("    Got expected exception: " + exc);
            if (exc.getCause() != null) {
                System.out.println("                with cause: " + exc.getCause());
            }
        } else {
            fail(exc, expectedExceptions);
        }
    }

    static boolean exceptionMatches(Throwable exc, Throwable expected) {
        return expected.getClass().isInstance(exc) &&
               Objects.equals(expected.getMessage(), exc.getMessage()) && (
                   expected.getCause() == null ||
                   exceptionMatches(exc.getCause(), expected.getCause())
               );
    }

    static void fail(Throwable thrownException, Throwable... expectedExceptions) {
        String msg;
        if (thrownException == null) {
            msg = "No exception thrown but there were expected exceptions (see suppressed)";
        } else if (expectedExceptions.length == 0) {
            msg = "Exception thrown (see cause) but there were no expected exceptions";
        } else {
            msg = "Exception thrown (see cause) but expected exceptions were different (see suppressed)";
        }
        AssertionError error = new AssertionError(msg, thrownException);
        Stream.of(expectedExceptions).forEach(error::addSuppressed);
        throw error;
    }

    static void doTest(Method m, Object target, Object[] args, Object expectedReturn, Throwable... expectedException) {
        MethodAccessor ma = ReflectionFactory.getReflectionFactory().newMethodAccessor(m);
        try {
            doTestAccessor(m, ma, target, args, expectedReturn, expectedException);
        } catch (Throwable e) {
            throw new RuntimeException(ma.getClass().getName() + " for method: " + m + " test failure", e);
        }
    }

    static void doTest(Constructor c, Object[] args, Object expectedReturn, Throwable... expectedExceptions) {
        ConstructorAccessor ca = ReflectionFactory.getReflectionFactory().newConstructorAccessor(c);
        try {
            doTestAccessor(c, ca, args, expectedReturn, expectedExceptions);
        } catch (Throwable e) {
            throw new RuntimeException(ca.getClass().getName() + " for constructor: " + c + " test failure", e);
        }
    }

    private final Throwable[] noException = new Throwable[0];
    private final Throwable[] mismatched_argument_type = new Throwable[] {
            new IllegalArgumentException("argument type mismatch")
    };
    private final Throwable[] mismatched_target_type = new Throwable[] {
            new IllegalArgumentException("argument type mismatch"),
            new IllegalArgumentException("object is not an instance of declaring class"),
    };

    private final Throwable[] wrong_argument_count = new Throwable[] {
            new IllegalArgumentException("wrong number of arguments"),
            new IllegalArgumentException("array is not of length 1")
    };
    private final Throwable[] null_argument = new Throwable[] {
            new IllegalArgumentException("wrong number of arguments"),
            new IllegalArgumentException("null array reference")
    };
    private final Throwable[] null_argument_value = new Throwable[] {
            new IllegalArgumentException()
    };
    private final Throwable[] null_target = new Throwable[] {
            new NullPointerException()
    };
    private final Throwable[] wrapped_npe_no_msg = new Throwable[]{
            new InvocationTargetException(new NullPointerException())
    };
    private final Throwable[] wrapped_npe = new Throwable[]{
            new InvocationTargetException(new NullPointerException("NPE"))
    };
    private final Throwable[] wrapped_cce = new Throwable[]{
            new InvocationTargetException(new ClassCastException("CCE"))
    };
    private final Throwable[] wrapped_iae = new Throwable[]{
            new InvocationTargetException(new IllegalArgumentException("IAE"))
    };


    @DataProvider(name = "testNoArgMethods")
    private Object[][] testNoArgMethods() {
        MethodHandleAccessorsTest inst = new MethodHandleAccessorsTest();
        Object[] emptyArgs = new Object[]{};
        return new Object[][] {
             new Object[] {"public_static_V",   null, emptyArgs, null, noException},
             new Object[] {"public_static_V",   null, null, null, noException},
             new Object[] {"public_static_I",   null, emptyArgs, 42, noException},
             new Object[] {"public_static_I",   null, null, 42, noException},
             new Object[] {"public_V",          inst, emptyArgs, null, noException},
             new Object[] {"public_V",          inst, null, null, noException},
             new Object[] {"public_I",          inst, emptyArgs, 42, noException},
             new Object[] {"public_I",          inst, null, 42, noException},
             new Object[] {"private_static_V",  null, emptyArgs, null, noException},
             new Object[] {"private_static_I",  null, emptyArgs, 42, noException},
             new Object[] {"private_V",         inst, emptyArgs, null, noException},
             new Object[] {"private_I",         inst, emptyArgs, 42, noException},
        };
    }

    @DataProvider(name = "testOneArgMethods")
    private Object[][] testOneArgMethods() {
        MethodHandleAccessorsTest inst = new MethodHandleAccessorsTest();
        Object wrongInst = new Object();
        boolean newImpl = Boolean.getBoolean("jdk.reflect.useDirectMethodHandle");
        System.out.println("TESTING old implementation " + newImpl);
        return new Object[][]{
            new Object[]{"public_static_I_V", int.class, null, new Object[]{12}, null, noException},
            new Object[]{"public_static_I_I", int.class, null, new Object[]{12}, 12, noException},
            new Object[]{"public_I_V", int.class,        inst, new Object[]{12}, null, noException},
            new Object[]{"public_I_I", int.class,        inst, new Object[]{12}, 12, noException},

            new Object[]{"private_static_I_V", int.class, null, new Object[]{12}, null, noException},
            new Object[]{"private_static_I_I", int.class, null, new Object[]{12}, 12, noException},
            new Object[]{"private_I_V", int.class,        inst, new Object[]{12}, null, noException},
            new Object[]{"private_I_I", int.class,        inst, new Object[]{12}, 12, noException},

            new Object[] {"public_static_I_I", int.class, null, new Object[]{"a"}, null, mismatched_argument_type},

            new Object[] {"public_I_I", int.class,        inst, new Object[]{"a"}, null, mismatched_argument_type},

            new Object[] {"public_static_I_I", int.class, null, new Object[]{12, 13}, null, wrong_argument_count},

            new Object[] {"public_I_I", int.class, inst, new Object[]{12, 13}, null, wrong_argument_count},
            // FIXME: does not match previous implementation
            new Object[] {"public_I_I", int.class, wrongInst, new Object[]{12}, 12,
                    newImpl ? mismatched_argument_type : mismatched_target_type},
            // FIXME: does not match previous implementation
            new Object[] {"public_I_I", int.class, null, new Object[]{12}, 12,
                    newImpl ? wrapped_npe_no_msg : null_target},

            new Object[] {"public_static_I_V", int.class, null, null, null, null_argument},

            new Object[] {"public_static_I_V", int.class, null, new Object[]{null}, null, null_argument_value},

            new Object[] {"public_I_I", int.class, inst, null, null, null_argument},

            new Object[] {"public_I_I", int.class, inst, new Object[]{null}, null, null_argument_value},
            new Object[] {"throws_exception", RuntimeException.class, null, new Object[]{new NullPointerException("NPE")}, null, wrapped_npe},
            new Object[] {"throws_exception", RuntimeException.class, null, new Object[]{new IllegalArgumentException("IAE")}, null, wrapped_iae},
            new Object[] {"throws_exception", RuntimeException.class, null, new Object[]{new ClassCastException("CCE")}, null, wrapped_cce},
        };
    }

    @DataProvider(name = "testMethodsWithVarargs")
    private Object[][] testMethodsWithVarargs() {
        Class<?>[] I_paramTypes = new Class<?>[] { int.class, int[].class };
        Class<?>[] L_paramTypes = new Class<?>[] { String.class, String[].class };
        return new Object[][]{
             new Object[] {"varargs_primitive", I_paramTypes, null, new Object[]{1, new int[]{2, 3}}, 105, noException},
             new Object[] {"varargs_primitive", I_paramTypes, null, new Object[]{1, new int[]{}}, 100, noException},
             new Object[] {"varargs_primitive", I_paramTypes, null, new Object[]{1, null}, 1, noException},
             new Object[] {"varargs_object", L_paramTypes,    null, new Object[]{"a", new String[]{"b", "c"}}, "a[b,c]", noException},
             new Object[] {"varargs_object", L_paramTypes,    null, new Object[]{"a", new String[]{}}, "a[]", noException},
             new Object[] {"varargs_object", L_paramTypes,    null, new Object[]{"a", null}, "a", noException},
        };
    }

    @Test(dataProvider = "testNoArgMethods")
    public void testNoArgMethod(String methodname, Object target, Object[] args, Object expectedReturn, Throwable[] expectedExpections) throws Exception {
        doTest(MethodHandleAccessorsTest.class.getDeclaredMethod(methodname), target, args, expectedReturn, expectedExpections);
    }

    @Test(dataProvider = "testOneArgMethods")
    public void testOneArgMethod(String methodname, Class<?> paramType, Object target, Object[] args, Object expectedReturn, Throwable[] expectedExpections) throws Exception {
        doTest(MethodHandleAccessorsTest.class.getDeclaredMethod(methodname, paramType), target, args, expectedReturn, expectedExpections);
    }

    @Test(dataProvider = "testMethodsWithVarargs")
    public void testMethodsWithVarargs(String methodname, Class<?>[] paramTypes, Object target, Object[] args, Object expectedReturn, Throwable[] expectedExpections) throws Exception {
        doTest(MethodHandleAccessorsTest.class.getDeclaredMethod(methodname, paramTypes), target, args, expectedReturn, expectedExpections);
    }

    @DataProvider(name = "testConstructors")
    private Object[][] testConstructors() {
        return new Object[][]{
                new Object[]{null, new Object[]{}, new Public(), noException},
                new Object[]{null, null, new Public(), noException},
                new Object[]{new Class<?>[]{int.class}, new Object[]{12}, new Public(12), noException},
                new Object[]{new Class<?>[]{String.class}, new Object[]{"a"}, new Public("a"), noException},


                new Object[]{new Class<?>[]{int.class, int[].class}, new Object[]{1, new int[]{2, 3}}, new Public(105), noException},
                new Object[]{new Class<?>[]{int.class, int[].class}, new Object[]{1, new int[]{}}, new Public(100), noException},
                new Object[]{new Class<?>[]{int.class, int[].class}, new Object[]{1, null}, new Public(1), noException},

                new Object[]{new Class<?>[]{String.class, String[].class}, new Object[]{"a", new String[]{"b", "c"}}, new Public("a[b,c]"), noException},
                new Object[]{new Class<?>[]{String.class, String[].class}, new Object[]{"a", new String[]{}}, new Public("a[]"), noException},
                new Object[]{new Class<?>[]{String.class, String[].class}, new Object[]{"a", null}, new Public("a"), noException},

                // test ConstructorAccessor exceptions thrown
                new Object[]{new Class<?>[]{int.class}, new Object[]{"a"}, null, mismatched_argument_type},
                new Object[]{new Class<?>[]{int.class}, new Object[]{12, 13}, null, wrong_argument_count},
                new Object[]{new Class<?>[]{int.class}, null, null, null_argument},
                new Object[]{new Class<?>[]{RuntimeException.class}, new Object[]{new NullPointerException("NPE")}, null, wrapped_npe},
                new Object[]{new Class<?>[]{RuntimeException.class}, new Object[]{new IllegalArgumentException("IAE")}, null, wrapped_iae},
                new Object[]{new Class<?>[]{RuntimeException.class}, new Object[]{new ClassCastException("CCE")}, null, wrapped_cce},
        };
    }

    @Test(dataProvider = "testConstructors")
    public void testPublicConstructors(Class<?>[] paramTypes, Object[] args, Object expectedReturn, Throwable[] expectedExpections) throws Exception {
        doTest(Public.class.getDeclaredConstructor(paramTypes), args, expectedReturn, expectedExpections);
    }

    @Test
    public void testOtherConstructors() throws Exception {
        doTest(Private.class.getDeclaredConstructor(), new Object[]{}, new Private());
        doTest(Private.class.getDeclaredConstructor(), null, new Private());
        doTest(Private.class.getDeclaredConstructor(int.class), new Object[]{12}, new Private(12));

        doTest(Abstract.class.getDeclaredConstructor(), null, null, new InstantiationException());
    }

    @Test
    public void testLambdaProxyClass() throws Exception {
        // test MethodAccessor on methods declared by hidden classes
        IntUnaryOperator intUnaryOp = i -> i;
        Method applyAsIntMethod = intUnaryOp.getClass().getDeclaredMethod("applyAsInt", int.class);
        doTest(applyAsIntMethod, intUnaryOp, new Object[]{12}, 12);
    }
}
