package java.util.ptype.util;

import java.util.NoSuchElementException;

/// Simplified implementation of [java.util.Optional] to avoid excluding the class from the prototype.
///
/// @param <T> the type of the potentially contained object
public final class Optional<T> {

    private final T value;

    private static final Optional<?> EMPTY = new Optional<>(null);

    private Optional(T value) {
        this.value = value;
    }

    /// Creates a new optional. This method throws if `value` is null.
    ///
    /// @param value the value
    /// @return the created optional
    /// @param <T> the type of the value
    public static <T> Optional<T> of(T value) {
        if (value == null) {
            throw new IllegalArgumentException("Cannot pass null");
        }
        return new Optional<>(value);
    }

    /// Creates a new optional from a potentially null value.
    ///
    /// @param value the value
    /// @return the optional
    /// @param <T> the type of the value
    static <T> Optional<T> ofNullable(T value) {
        return value == null ? empty() : new Optional<>(value);
    }

    /// Returns an empty optional.
    ///
    /// @return an empty optional
    /// @param <T> The type of the content
    @SuppressWarnings("unchecked")
    public static <T> Optional<T> empty() {
        return (Optional<T>) EMPTY;
    }

    /// Gets the element from this optional or throws if it is empty.
    ///
    /// @return the element
    public T get() {
        if (value == null) {
            throw new NoSuchElementException();
        }
        return value;
    }

    /// Returns whether this optional has a value.
    ///
    /// @return true if this optional has a value; false otherwise
    public boolean isPresent() {
        return value != null;
    }

    /// Returns whether this optional is empty.
    ///
    /// @return true if this optional is empty; false otherwise
    public boolean isEmpty() {
        return value == null;
    }

}
