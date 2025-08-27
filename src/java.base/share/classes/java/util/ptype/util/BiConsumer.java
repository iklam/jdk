package java.util.ptype.util;

/// BiConsumer.
///
/// @param <E> consumer first type
/// @param <F> consumer second type
@FunctionalInterface
public interface BiConsumer<E, F> {

    /// Accept the parameters.
    ///
    /// @param p1 the first parameter
    /// @param p2 the second parameter
    void accept(E p1, F p2);

}
