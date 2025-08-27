package java.util.ptype.model;


import java.util.ptype.util.ArrayList;
import java.util.ptype.util.Utils;

/// Represents a parameterized type.
public final class ParameterizedType extends ConcreteSpecializedType implements SpecializedType {

    private final Class<?> rawType;

    private final ArrayList<SpecializedType> actualTypeArguments;

    /// Creates a new [ParameterizedType].
    ///
    /// @param rawType the raw type
    /// @param actualTypeArguments the type arguments
    public ParameterizedType(Class<?> rawType, SpecializedType... actualTypeArguments) {
        Utils.requireNonNull(rawType);
        Utils.requireNonNull(actualTypeArguments);
        this.rawType = rawType;
        this.actualTypeArguments = ArrayList.of(actualTypeArguments);
    }

    /// Creates a new [ParameterizedType].
    ///
    /// @param rawType the raw type as a string
    /// @param actualTypeArguments the type arguments
    public ParameterizedType(String rawType, SpecializedType... actualTypeArguments) {
        Utils.requireNonNull(rawType);
        Utils.requireNonNull(actualTypeArguments);
        this.rawType = Utils.findClassByName(rawType);
        this.actualTypeArguments = ArrayList.of(actualTypeArguments);
    }

    /// Gets the raw type.
    ///
    /// @return the raw type
    public Class<?> rawType() {
        return rawType;
    }

    /// Gets the type arguments.
    ///
    /// @return the type arguments
    public ArrayList<SpecializedType> actualTypeArguments() {
        return actualTypeArguments;
    }

    //    public boolean isAssignable(Arg actual, Variance variance) {
//        Utils.requireNonNull(actual);
//        Utils.requireNonNull(variance);
//        if (variance == Variance.INVARIANT) {
//            // TODO ignore arguments ?
//            return (actual instanceof RawType rt && type.equals(rt.type()))
//                    || (actual instanceof ParameterizedType parameterizedType && type.equals(parameterizedType.rawType())
//                    && compareArguments(parameterizedType));
//        }
//        if (actual instanceof ParameterizedType parameterizedType) {
//            return compareClass(
//                    parameterizedType.rawType(),
//                    variance
//            ) && compareArguments(parameterizedType);
//            // TODO ignore arguments ?
//        } else if (actual instanceof RawType rt) {
//            return compareClass(rt.type(), variance);
//        } else if (actual instanceof Wildcard wildcard) {
//            if (variance == Variance.COVARIANT) {
//                return wildcard.upperBound().anyMatch(Utils.isAssignableLambdaExpected(this, variance));
//            } else if (variance == Variance.CONTRAVARIANT) {
//                return wildcard.lowerBound().anyMatch(Utils.isAssignableLambdaExpected(this, variance));
//            }
//            throw new IllegalArgumentException();
//        } else if (actual instanceof Intersection intersection) {
//            return intersection.bounds().anyMatch(Utils.isAssignableLambdaExpected(this, variance));
//        } else if (actual instanceof InnerClassType innerClassType) {
//            return isAssignable(innerClassType.innerType(), variance);
//        } else if (actual instanceof ClassType classType) {
//            return compareClass(classType.type(), variance)
//                    && Internal.staticArgs(classType.type()).anyMatch(Utils.isAssignableLambdaExpected(
//                    this,
//                    Variance.INVARIANT
//            ));
//        } else if (actual instanceof ArrayType) {
//            return false;
//        }
//        throw new IllegalArgumentException();

//    }

}
