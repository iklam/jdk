package java.util.ptype.model;

/// Supertype for all specialized types.
public sealed interface SpecializedType
        extends SpecializedTypeContainer
        permits ArrayType, ClassType, InnerClassType, IntersectionType, ParameterizedType, RawType, WildcardType {

}
