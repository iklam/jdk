package java.util.ptype.model;

import java.util.Objects;
import java.util.ptype.SpecializedTypeUtils;
import java.util.ptype.util.ArrayList;
import java.util.ptype.util.Utils;

/// Represents the type arguments of a method.
public final class SpecializedMethodTypeArguments implements SpecializedTypeContainer {

    private final ArrayList<SpecializedType> arguments;

    public SpecializedMethodTypeArguments(SpecializedType... arguments) {
        this.arguments = ArrayList.of(Utils.requireNonNull(arguments));
    }

    public SpecializedType typeArgument(int index) {
        Objects.checkIndex(arguments.size(), index);
        return arguments.get(index);
    }

    @Override
    public String toString() {
        var builder = new StringBuilder();
        builder.append("<");
        arguments.joinTo(builder, SpecializedTypeUtils::appendToBuilder, ", ");
        builder.append(">");
        return builder.toString();
    }

}
