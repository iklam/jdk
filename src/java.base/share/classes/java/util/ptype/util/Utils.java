package java.util.ptype.util;

import java.util.Objects;

/// Utility class.
public final class Utils {

    /// Checks whether a given value is null. If it is null, throws an exception.
    ///
    /// @param o the value to check
    /// @return the value is it is not null
    /// @param <T> the type of the value
    public static <T> T requireNonNull(T o) {
        if (o == null) {
            throw new NullPointerException();
        }
        return o;
    }

    /// Finds a class from its binary name.
    ///
    /// @param name the binary name of the class
    /// @return the corresponding clas
    public static Class<?> findClassByName(String name) {
        Objects.requireNonNull(name);
        try {
            return Class.forName(name, false, ClassLoader.getPlatformClassLoader());
        } catch (ClassNotFoundException e) {
            throw new AssertionError("Could not load: " + name, e);
        }
    }

    private Utils() {
        throw new AssertionError();
    }

}
