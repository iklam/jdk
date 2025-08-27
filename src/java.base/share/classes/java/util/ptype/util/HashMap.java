package java.util.ptype.util;

import jdk.internal.vm.annotation.Stable;

/// Immutable hashmap implementation.
///
/// @param <K> the type of the keys
/// @param <V> the type of the values
public final class HashMap<K,V>  {

    @Stable
    private final Object[] table;

    @Stable
    private final int size;

    /// Creates a new hashmap from the given entries.
    ///
    /// @param input the entries to populate the map
    public HashMap(Object... input) {
        if ((input.length & 1) != 0) {
            throw new IllegalArgumentException("length is odd");
        }
        size = input.length >> 1;

        int len = 2 * input.length;
        len = (len + 1) & ~1; // ensure table is even length
        table = new Object[len];

        for (int i = 0; i < input.length; i += 2) {
            @SuppressWarnings("unchecked")
            K k = Utils.requireNonNull((K)input[i]);
            @SuppressWarnings("unchecked")
            V v = Utils.requireNonNull((V)input[i+1]);
            int idx = probe(k);
            if (idx >= 0) {
                throw new IllegalArgumentException("duplicate key: " + k);
            } else {
                int dest = -(idx + 1);
                table[dest] = k;
                table[dest+1] = v;
            }
        }
    }

    @Override
    public int hashCode() {
        int hash = 0;
        for (int i = 0; i < table.length; i += 2) {
            Object k = table[i];
            if (k != null) {
                hash += k.hashCode() ^ table[i + 1].hashCode();
            }
        }
        return hash;
    }

    /// Gets the value associated to a given key
    ///
    /// @param o the key
    /// @return the associated value or null if the key does not exist
    @SuppressWarnings("unchecked")
    public V get(Object o) {
        if (size == 0) {
            Utils.requireNonNull(o);
            return null;
        }
        int i = probe(o);
        if (i >= 0) {
            return (V)table[i+1];
        } else {
            return null;
        }
    }

    /// Tests whether the map is empty.
    ///
    /// @return true if the map is empty; false otherwise.
    public boolean isEmpty() {
        return size == 0;
    }

    private int probe(Object pk) {
        int idx = Math.floorMod(pk.hashCode(), table.length >> 1) << 1;
        while (true) {
            @SuppressWarnings("unchecked")
            K ek = (K)table[idx];
            if (ek == null) {
                return -idx - 1;
            } else if (pk.equals(ek)) {
                return idx;
            } else if ((idx += 2) == table.length) {
                idx = 0;
            }
        }
    }

}