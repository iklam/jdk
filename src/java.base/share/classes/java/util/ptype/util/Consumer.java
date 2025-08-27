package java.util.ptype.util;

/// Consumer.
///
/// @param <E> consumer type
@FunctionalInterface
public interface Consumer<E> {

    /// Accept the parameter.
    ///
    /// @param p1 the parameter
    void accept(E p1);

}
