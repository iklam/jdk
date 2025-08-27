package java.util.ptype.model;

import java.util.ptype.util.Utils;

/// Represents a raw type.
public final class RawType extends ConcreteSpecializedType implements SpecializedType {

    private final Class<?> rawType;

    /// Creates a new RawType.
    ///
    /// @param rawType the raw type
    public RawType(Class<?> rawType) {
        Utils.requireNonNull(rawType);
        this.rawType = rawType;
    }

    /// Creates a new RawType from the class name
    ///
    /// @param rawType the raw type
    public RawType(String rawType) {
        this(Utils.findClassByName(Utils.requireNonNull(rawType)));
    }

    /// Gets the raw type of this raw type.
    ///
    /// @return the raw type
    public Class<?> type() {
        return rawType;
    }

//    /**
//     * Creates a {@link RawType} from the given raw type.
//     * <p>
//     * This method is only used for testing purposes. RawType should be created statically by the compiler, so this
//     * dynamic method should never be called.
//     *
//     * @param type the raw type
//     * @return the {@link RawType}
//     */
//    static RawType of(Class<?> type) {
//        java.util.ptype.util.Utils.requireNonNull(type);
//        return new RawType() {
//            @Override
//            public Class<?> type() {
//                return type;
//            }
//
//            @Override
//            public boolean isAssignable(Arg actual, Variance variance) {
//                java.util.ptype.util.Utils.requireNonNull(actual);
//                if (variance == Variance.INVARIANT) {
//                    return (actual instanceof RawType rawType && type.equals(rawType.type()))
//                           || (actual instanceof ParameterizedType parameterizedType && type.equals(parameterizedType.rawType()));
//                }
//                if (actual instanceof ParameterizedType parameterizedType) {
//                    return compareClass(parameterizedType.rawType(), variance);
//                } else if (actual instanceof RawType rawType) {
//                    return compareClass(rawType.type(), variance);
//                } else if (actual instanceof Intersection intersection) {
//                    return intersection.bounds()
//                        .anyMatch(java.util.ptype.util.Utils.isAssignableLambdaExpected(this, variance));
//                } else if (actual instanceof Wildcard wildcard) {
//                    if (variance == Variance.COVARIANT) {
//                        return wildcard.upperBound().anyMatch(java.util.ptype.util.Utils.isAssignableLambdaExpected(this, variance));
//                    } else if (variance == Variance.CONTRAVARIANT) {
//                        return wildcard.lowerBound().anyMatch(java.util.ptype.util.Utils.isAssignableLambdaExpected(this, variance));
//                    }
//                    throw new IllegalArgumentException();
//                } else if (actual instanceof InnerClassType innerClassType) {
//                    return isAssignable(innerClassType.innerType(), variance);
//                } else if (actual instanceof ArrayType) {
//                    return false;
//                } else if (actual instanceof ClassType classType) {
//                    return compareClass(classType.type(), variance);
//                }
//                throw new IllegalArgumentException();
//            }
//
//            private boolean compareClass(Class<?> clazz, Variance variance) {
//                if (variance == Variance.INVARIANT) {
//                    throw new AssertionError("Should not reach here");
//                } else if (variance == Variance.COVARIANT) {
//                    return type.isAssignableFrom(clazz);
//                } else if (variance == Variance.CONTRAVARIANT) {
//                    return clazz.isAssignableFrom(type);
//                }
//                throw new IllegalArgumentException();
//            }
//
//            @Override
//            public void appendTo(StringBuilder builder) {
//                java.util.ptype.util.Utils.requireNonNull(builder);
//                builder.append(type.getSimpleName());
//                builder.append("<raw type>");
//            }
//
//            @Override
//            public String toString() {
//                return Arg.toString(this);
//            }
//        };
//    }

}
