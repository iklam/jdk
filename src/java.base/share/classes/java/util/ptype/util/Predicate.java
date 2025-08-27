package java.util.ptype.util;

/// Predicate
///
/// @param <E> the type to test
@FunctionalInterface
public interface Predicate<E> {

    /// Test the argument.
    ///
    /// @param p1 the argument to test
    /// @return true if the test is passed, false otherwise
    boolean test(E p1);

}