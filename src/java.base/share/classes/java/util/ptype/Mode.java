package java.util.ptype;

enum Mode {
        /// All casts are generated.
        FULL,
        /// Only casts for type parameters at method entry and exit, and cast are generated.
        NORMAL,
        /// Only storage casts are generated.
        MINIMAL,
        /// No cast is generated.
        DISABLED,
        ;

}
