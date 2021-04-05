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

import java.io.IOException;
import java.io.OutputStream;
import java.io.UncheckedIOException;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

import jdk.internal.access.JavaLangInvokeAccess;
import jdk.internal.access.SharedSecrets;
import jdk.internal.misc.Unsafe;
import jdk.internal.misc.VM;
import jdk.internal.vm.annotation.Hidden;
import sun.security.action.GetBooleanAction;
import sun.security.action.GetPropertyAction;

import static java.lang.invoke.MethodType.methodType;

final class MethodHandleAccessorFactory {
    private static final Unsafe UNSAFE = Unsafe.getUnsafe();

    static MethodAccessorImpl newMethodAccessor(Method method, boolean callerSensitive) {
        if (!VM.isJavaLangInvokeInited() || Modifier.isNative(method.getModifiers())) {
            return DirectMethodAccessorImpl.nativeAccessor(method, callerSensitive);
        }

        // ExceptionInInitializerError may be thrown during class initialization
        // Ensure class initialized outside the invocation of method handle
        // so that EIIE is propagated (not wrapped with ITE)
        UNSAFE.ensureClassInitialized(method.getDeclaringClass());
        try {
            if (callerSensitive) {
                var dmh = findDirectMethodWithLeadingCaller(method);
                if (dmh != null) {
                    var mhInvoker = newMethodHandleAccessor(method, dmh, true);
                    var accessor = DirectMethodAccessorImpl.callerSensitiveAdapter(method, dmh, mhInvoker);
                    if (VERBOSE) {
                        System.out.println(method + " dmh " + dmh);
                    }
                    return accessor;
                }
            }
            var dmh = getDirectMethod(method, callerSensitive);
            if (callerSensitive) {
                return DirectMethodAccessorImpl.callerSensitiveMethodAccessor(method, dmh);
            } else {
                var mhInvoker = newMethodHandleAccessor(method, dmh, false);
                return DirectMethodAccessorImpl.methodAccessor(method, dmh, mhInvoker);
            }
        } catch (IllegalAccessException e) {
            throw new InternalError(e);
        }
    }

    static ConstructorAccessorImpl newConstructorAccessor(Constructor<?> ctor) {
        if (!VM.isJavaLangInvokeInited()) {
            return DirectConstructorAccessorImpl.nativeAccessor(ctor);
        }

        // ExceptionInInitializerError may be thrown during class initialization
        // Ensure class initialized outside the invocation of method handle
        // so that EIIE is propagated (not wrapped with ITE)
        UNSAFE.ensureClassInitialized(ctor.getDeclaringClass());
        try {
            MethodHandle mh = JLIA.unreflectConstructor(ctor);
            int paramCount = mh.type().parameterCount();
            MethodHandle target = MethodHandles.catchException(mh, Throwable.class,
                    WRAP.asType(methodType(mh.type().returnType(), Throwable.class)));
            target = target.asSpreader(Object[].class, paramCount)
                           .asType(methodType(Object.class, Object[].class));
            return DirectConstructorAccessorImpl.constructorAccessor(ctor, target);
        } catch (IllegalAccessException e) {
            throw new InternalError(e);
        }
    }

    static FieldAccessorImpl newFieldAccessor(Field field, boolean isReadOnly) {
        // ExceptionInInitializerError may be thrown during class initialization
        // Ensure class initialized outside the invocation of method handle
        // so that EIIE is propagated (not wrapped with ITE)
        UNSAFE.ensureClassInitialized(field.getDeclaringClass());
        try {
            var accessor = newVarHandleAccessor(field);

            Class<?> type = field.getType();
            if (type == Boolean.TYPE) {
                return new VarHandleBooleanFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Byte.TYPE) {
                return new VarHandleByteFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Short.TYPE) {
                return new VarHandleShortFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Character.TYPE) {
                return new VarHandleCharacterFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Integer.TYPE) {
                return new VarHandleIntegerFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Long.TYPE) {
                return new VarHandleLongFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Float.TYPE) {
                return new VarHandleFloatFieldAccessorImpl(field, accessor, isReadOnly);
            } else if (type == Double.TYPE) {
                return new VarHandleDoubleFieldAccessorImpl(field, accessor, isReadOnly);
            } else {
                return new VarHandleObjectFieldAccessorImpl(field, accessor, isReadOnly);
            }
        } catch (IllegalAccessException e) {
            throw new InternalError(e);
        }
    }

    private static MethodHandle getDirectMethod(Method method, boolean callerSensitive) throws IllegalAccessException {
        var mtype = methodType(method.getReturnType(), method.getParameterTypes());
        var isStatic = Modifier.isStatic(method.getModifiers());
        var dmh = isStatic ? JLIA.findStatic(method.getDeclaringClass(), method.getName(), mtype)
                                        : JLIA.findVirtual(method.getDeclaringClass(), method.getName(), mtype);
        if (callerSensitive) {
            // the reflectiveInvoker for caller-sensitive method expects the same signature
            // as Method::invoke i.e. (Object, Object[])Object
            return makeTarget(dmh, isStatic, false);
        }
        if (method.isVarArgs() && method.getParameterCount() == 1) {
            MethodType type = isStatic  ? methodType(Object.class, Object.class)
                                        : methodType(Object.class, Object.class, Object.class);
            return dmh.asFixedArity().asType(type);
        }

        return makeSpecializedTarget(dmh, isStatic, false);
    }

    private static MethodHandle findDirectMethodWithLeadingCaller(Method method) throws IllegalAccessException {
        String name = method.getName();
        // insert the leading Class parameter
        MethodType mtype = methodType(method.getReturnType(), method.getParameterTypes())
                                .insertParameterTypes(0, Class.class);
        boolean isStatic = Modifier.isStatic(method.getModifiers());
        MethodHandle dmh = isStatic ? JLIA.findStatic(method.getDeclaringClass(), name, mtype)
                                    : JLIA.findVirtual(method.getDeclaringClass(), name, mtype);
        return dmh != null ? makeSpecializedTarget(dmh, isStatic, true) : null;
    }

    /**
     * Transform the given dmh to a specialized target method handle.
     *
     * If {@code hasLeadingCaller} parameter is true, transform the method handle
     * of this method type: {@code (Object, Class, Object[])Object} for the default
     * case.
     *
     * If {@code hasLeadingCaller} parameter is false, transform the method handle
     * of this method type: {@code (Object, Object[])Object} for the default case.
     *
     * If the number of formal arguments is small, use a method type specialized
     * the number of formal arguments is 0, 1, and 2, for example, the method type
     * of a static method with one argument can be: {@code (Object)Object}
     *
     * If it's a static method, there is no leading Object parameter.
     *
     * @apiNote
     * This implementation avoids using MethodHandles::catchException to help
     * cold startup performance since this combination is very costly to setup.
     *
     * @param dmh DirectMethodHandle
     * @param isStatic whether given dmh represents static method or not
     * @param hasLeadingCaller whether given dmh represents a method with leading
     *                         caller Class parameter
     * @return transformed dmh to be used as a target in direct method accessors
     */
    static MethodHandle makeSpecializedTarget(MethodHandle dmh, boolean isStatic, boolean hasLeadingCaller) {
        MethodHandle target = dmh.asFixedArity();

        // number of formal arguments to the original method (not the adapter)
        // If it is a non-static method, it has a leading `this` argument.
        // Also do not count the caller class argument
        int paramCount = dmh.type().parameterCount() - (isStatic ? 0 : 1) - (hasLeadingCaller ? 1 : 0);
        MethodType mtype = specializedMethodType(isStatic, hasLeadingCaller, paramCount);
        if (paramCount > SPECIALIZED_PARAM_COUNT) {
            // spread the last "real" parameters (not counting leading 'this' and 'caller')
            target = target.asSpreader(Object[].class, paramCount);
        }
        return target.asType(mtype);
    }

    private static final int SPECIALIZED_PARAM_COUNT = 2;
    static MethodType specializedMethodType(boolean isStatic, boolean hasLeadingCaller, int paramCount) {
        return switch (paramCount) {
            // specialize for number of formal arguments <= 2 to avoid spreader
            case 0 -> isStatic ? (hasLeadingCaller ? methodType(Object.class, Class.class)
                                                   : methodType(Object.class))
                               : (hasLeadingCaller ? methodType(Object.class, Object.class, Class.class)
                                                   : methodType(Object.class, Object.class));
            case 1 -> isStatic ? (hasLeadingCaller ? methodType(Object.class, Class.class, Object.class)
                                                   : methodType(Object.class, Object.class))
                               : (hasLeadingCaller ? methodType(Object.class, Object.class, Class.class, Object.class)
                                                   : methodType(Object.class, Object.class, Object.class));
            case 2 -> isStatic ? (hasLeadingCaller ? methodType(Object.class, Class.class, Object.class, Object.class)
                                                   : methodType(Object.class, Object.class, Object.class))
                               : (hasLeadingCaller ? methodType(Object.class, Object.class, Class.class, Object.class, Object.class)
                                                   : methodType(Object.class, Object.class, Object.class, Object.class));
            default -> isStatic ? (hasLeadingCaller ? methodType(Object.class, Class.class, Object[].class)
                                                    : methodType(Object.class, Object[].class))
                                : (hasLeadingCaller ? methodType(Object.class, Object.class, Class.class, Object[].class)
                                                    : methodType(Object.class, Object.class, Object[].class));
        };
    }
    /**
     * Transforms the given dmh into a target method handle with the method type
     * {@code (Object, Object[])Object} or {@code (Object, Class, Object[])Object}
     */
    static MethodHandle makeTarget(MethodHandle dmh, boolean isStatic, boolean hasLeadingCaller) {
        MethodType mtype = hasLeadingCaller
                                ? methodType(Object.class, Object.class, Class.class, Object[].class)
                                : methodType(Object.class, Object.class, Object[].class);
        // number of formal arguments
        int paramCount = dmh.type().parameterCount() - (isStatic ? 0 : 1) - (hasLeadingCaller ? 1 : 0);
        MethodHandle target = dmh.asFixedArity().asSpreader(Object[].class, paramCount);
        if (isStatic) {
            // add leading 'this' parameter to static method which is then ignored
            target = MethodHandles.dropArguments(target, 0, Object.class);
        }
        return target.asType(mtype);
    }

    /**
     * Spins a hidden class that invokes a constant VarHandle of the target field,
     * loaded from the class data via condy, for reliable performance
     */
    private static MHFieldAccessor newVarHandleAccessor(Field field) throws IllegalAccessException {
        var varHandle = JLIA.unreflectVarHandle(field);
        var name = classNamePrefix(field);
        var cn = name + "$$" + counter.getAndIncrement();
        byte[] bytes = ACCESSOR_CLASSFILES.computeIfAbsent(name, n -> spinByteCode(cn, field));
        try {
            var lookup = JLIA.defineHiddenClassWithClassData(LOOKUP, cn, bytes, varHandle, true);
            var ctor = lookup.findConstructor(lookup.lookupClass(), methodType(void.class));
            ctor = ctor.asType(methodType(MHFieldAccessor.class));
            return (MHFieldAccessor) ctor.invokeExact();
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    /**
     * Spins a hidden class that invokes a constant MethodHandle of the target method handle,
     * loaded from the class data via condy, for reliable performance.
     *
     * Due to the overhead of class loading, this is not the default.
     */
    private static MHMethodAccessor newMethodHandleAccessor(Method method, MethodHandle target, boolean hasLeadingCaller) {
        if (!ReflectionFactory.spinMHAccessorClass()) {
            return new MHMethodAccessorDelegate(target);
        }
        var name = classNamePrefix(method, target.type(), hasLeadingCaller);
        var cn = name + "$$" + counter.getAndIncrement();
        byte[] bytes = ACCESSOR_CLASSFILES.computeIfAbsent(name, n -> spinByteCode(cn, method, target.type(), hasLeadingCaller));
        try {
            var lookup = JLIA.defineHiddenClassWithClassData(LOOKUP, cn, bytes, target, true);
            var ctor = lookup.findConstructor(lookup.lookupClass(), methodType(void.class));
            ctor = ctor.asType(methodType(MHMethodAccessor.class));
            return (MHMethodAccessor) ctor.invokeExact();
        } catch (Throwable e) {
            throw new InternalError(e);
        }
    }

    private static final ConcurrentHashMap<String, byte[]> ACCESSOR_CLASSFILES = new ConcurrentHashMap<>();
    private static final String FIELD_CLASS_NAME_PREFIX = "jdk/internal/reflect/FieldAccessorImpl_";
    private static final String METHOD_CLASS_NAME_PREFIX = "jdk/internal/reflect/MethodAccessorImpl_";
    // Used to ensure that each spun class name is unique
    private static final AtomicInteger counter = new AtomicInteger();

    private static String classNamePrefix(Field field) {
        var isStatic = Modifier.isStatic(field.getModifiers());
        var type = field.getType();
        var desc = type.isPrimitive() ? type.descriptorString() : "L";
        return FIELD_CLASS_NAME_PREFIX + (isStatic ? desc : "L" + desc);
    }
    private static String classNamePrefix(Method method, MethodType mtype, boolean hasLeadingCaller) {
        var isStatic = Modifier.isStatic(method.getModifiers());
        var methodTypeName = methodTypeName(isStatic, hasLeadingCaller, mtype);
        return METHOD_CLASS_NAME_PREFIX + methodTypeName;
    }

    /**
     * Returns a string to represent the specialized method type.
     */
    private static String methodTypeName(boolean isStatic, boolean hasLeadingCaller, MethodType mtype) {
        StringBuilder sb = new StringBuilder();
        int pIndex = 0;
        if (!isStatic) {
            sb.append("L");
            pIndex++;
        }
        if (hasLeadingCaller) {
            sb.append("Class");
            pIndex++;
        }
        for (;pIndex < mtype.parameterCount(); pIndex++) {
            Class<?> ptype = mtype.parameterType(pIndex);
            if (ptype == Object[].class) {
                sb.append("A");
            } else {
                assert ptype == Object.class;
                sb.append("L");
            }
        }
        return sb.toString();
    }

    private static byte[] spinByteCode(String cn, Field field) {
        var builder = new ClassByteBuilder(cn, VarHandle.class);
        var bytes = builder.buildFieldAccessor(field);
        maybeDumpClassFile(cn, bytes);
        return bytes;
    }
    private static byte[] spinByteCode(String cn, Method method, MethodType mtype, boolean hasLeadingCaller) {
        var builder = new ClassByteBuilder(cn, MethodHandle.class);
        var bytes = builder.buildMethodAccessor(method, mtype, hasLeadingCaller);
        maybeDumpClassFile(cn, bytes);
        return bytes;
    }
    private static void maybeDumpClassFile(String classname, byte[] bytes) {
        if (DUMP_CLASS_FILES != null) {
            try {
                Path p = DUMP_CLASS_FILES.resolve(classname + ".class");
                Files.createDirectories(p.getParent());
                try (OutputStream os = Files.newOutputStream(p)) {
                    os.write(bytes);
                }
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
    }

    // make this package-private to workaround a bug in Reflection::getCallerClass
    // that skips this class and the lookup class is ReflectionFactory instead
    // this frame is hidden not to alter the stacktrace for InvocationTargetException
    // so that the stack trace of its cause is the same as ITE.
    @Hidden
    static Object wrap(Throwable e) throws InvocationTargetException {
        throw new InvocationTargetException(e);
    }

    static MethodHandle reflectiveInvokerFor(Object obj) {
        return METHOD_ACCESSOR_INVOKE.bindTo(obj);
    }

    private static final MethodHandles.Lookup LOOKUP = MethodHandles.lookup();
    private static final JavaLangInvokeAccess JLIA;
    private static final MethodHandle WRAP;
    private static MethodHandle METHOD_ACCESSOR_INVOKE;
    private static final Path DUMP_CLASS_FILES;

    static boolean VERBOSE = GetBooleanAction.privilegedGetProperty("jdk.reflect.debug");

    static {
        try {
            JLIA = SharedSecrets.getJavaLangInvokeAccess();
            WRAP = LOOKUP.findStatic(MethodHandleAccessorFactory.class, "wrap",
                                                     methodType(Object.class, Throwable.class));
            METHOD_ACCESSOR_INVOKE = JLIA.findVirtual(MethodAccessorImpl.class, "invoke",
                                                     methodType(Object.class, Object.class, Object[].class));
        } catch (NoSuchMethodException|IllegalAccessException e) {
            throw new InternalError(e);
        }
        String dumpPath = GetPropertyAction.privilegedGetProperty("jdk.reflect.dumpClass");
        if (dumpPath != null) {
            DUMP_CLASS_FILES = Path.of(dumpPath);
        } else {
            DUMP_CLASS_FILES = null;
        }
    }
}

