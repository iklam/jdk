# Copyright (c) 2026, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#


# Benchmark for:
#       JDK-8353598: Iterative AOT Training
#       JDK-8335368: Ahead-of-Time Code Compilation
#
# Usage:
#
# bash aotbench.sh /path/to/java
#
# - Train a AOT caches with compiling two types ("A" and "B") of Java source code.
#     Single step training:
#     - a.aot: trained with only type "A"
#     - b.aot: trained with only type "B"
#
#     Iterative training:
#    - ab.aot: with a.aot, training with type "B"
#    - ba.aot: with b.aot, training with type "A"

# Here are some sample data, collected on GIT version ea1e7c5a9e75fe09745feca451c063cc95960b33
# with ubuntu 24.04, Intel(R) Core(TM) i7-14700, 64GB RAM
#
# ==== no AOT ==============
#
# 'java com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.25291 +- 0.00136 seconds time elapsed  ( +-  0.54% )
# 'java com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.25170 +- 0.00122 seconds time elapsed  ( +-  0.48% )
# 
# ==== AOT cache, AOT code DISABLED ===========
# 
# 'java -XX:AOTCache=a.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.10808 +- 0.00171 seconds time elapsed  ( +-  1.58% )
# 'java -XX:AOTCache=a.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.15670 +- 0.00126 seconds time elapsed  ( +-  0.80% )
# 
# 'java -XX:AOTCache=b.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.14393 +- 0.00147 seconds time elapsed  ( +-  1.02% )
# 'java -XX:AOTCache=b.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.117853 +- 0.000533 seconds time elapsed  ( +-  0.45% )
# 
# 'java -XX:AOTCache=ab.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.13652 +- 0.00164 seconds time elapsed  ( +-  1.20% )
# 'java -XX:AOTCache=ab.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.12144 +- 0.00107 seconds time elapsed  ( +-  0.88% )
# 
# 'java -XX:AOTCache=ba.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.132192 +- 0.000710 seconds time elapsed  ( +-  0.54% )
# 'java -XX:AOTCache=ba.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.12113 +- 0.00138 seconds time elapsed  ( +-  1.14% )
# 
# ==== AOT cache, AOT code ENABLED ============
# 
# 'java -XX:AOTCache=a.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.10058 +- 0.00106 seconds time elapsed  ( +-  1.05% )
# 'java -XX:AOTCache=a.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.100661 +- 0.000392 seconds time elapsed  ( +-  0.39% )
# 
# 'java -XX:AOTCache=b.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.11509 +- 0.00116 seconds time elapsed  ( +-  1.01% )
# 'java -XX:AOTCache=b.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.090042 +- 0.000382 seconds time elapsed  ( +-  0.42% )
# 
# 'java -XX:AOTCache=ab.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.09866 +- 0.00107 seconds time elapsed  ( +-  1.08% )
# 'java -XX:AOTCache=ab.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.085682 +- 0.000288 seconds time elapsed  ( +-  0.34% )
# 
# 'java -XX:AOTCache=ba.aot com.sun.tools.javac.Main SourceA1.java' (25 runs):
#         0.10064 +- 0.00127 seconds time elapsed  ( +-  1.26% )
# 'java -XX:AOTCache=ba.aot com.sun.tools.javac.Main SourceB1.java' (25 runs):
#         0.086328 +- 0.000353 seconds time elapsed  ( +-  0.41% )

if test "$1" = ""; then
    echo Usage: bash $0 /path/to/java
    exit
fi

JAVA=$1; shift

repeat=$1; shift
if test "$repeat" = ""; then
    repeat=25
fi

SRCA=/tmp/aotbench/srcA
SRCB=/tmp/aotbench/srcB

rm -rf $SRCA $SRCB
mkdir -p $SRCA $SRCB

for i in {1..10}; do

    cat <<EOF > $SRCA/SourceA$i.java
import module java.base;
import static java.lang.String.format;

class SourceA$i {
    // Some comments
    static long x;
    static final long y;
    static {
        y = System.currentTimeMillis();
    }

    /* More comments */
    String func() { return "String " + this + y; }
    public static void main(String args[]) {
        try {
            x = Long.parseLong(args[0]);
        } catch (Throwable t) {
            t.printStackTrace();
        }
    }
    @Deprecated
    class InnerClass1 {
        static final long yy = y;
        Object N = new Object() {
            void a() {}
        };
    }
    enum Expression {
        ADDITION,
        SUBTRACTION,
        MULTIPLICATION,
        DIVISION
    }

    String html = """
          <html>
            <body>
              <p>Hello, world</p>
            </body>
          </html>
          """;
    static String[][] a = {
        {"a", "a"},
        {"a", "a"},
        {"a", "a"},
        {"a", "a"},
    };
}
EOF

    cat <<EOF > $SRCB/SourceB$i.java
import java.lang.*;
import java.util.*;

class SourceB$i {
    public static void main(String args[]) {
        doit(() -> {
                System.out.println("Hello Lambda");
                Thread.dumpStack();
            });
    }   
    static List<String> list = List.of("1", "2");

    static void doit(Runnable r) {
        for (var x : list) {
            r.run();
        }
    }
    static String patternMatch(String arg, Object o) {
        if (o instanceof String s) {
            return "1234";
        }
        if (o instanceof Point(int x, int y)) {
            System.out.println("Coordinates: " + x + ", " + y);
        }
        final String b = "B";
        return switch (arg) {
        case "A" -> "a";
        case b   -> "b";
        default  -> "c";
        };
    }
    public sealed class SealedInnerClass {}
    public final class Foo extends SealedInnerClass {}

    public record Point(int x, int y) {
        public Point(int x) {
            this(x, 0);
        }
    }
}
EOF
done

$JAVA -XX:AOTMode=record -XX:AOTCacheOutput=a.aot com.sun.tools.javac.Main -d /tmp $SRCA/*
$JAVA -XX:AOTMode=record -XX:AOTCacheOutput=b.aot com.sun.tools.javac.Main -d /tmp $SRCB/*
$JAVA -XX:AOTMode=record -XX:AOTCache=a.aot -XX:AOTCacheOutput=ab.aot com.sun.tools.javac.Main -d /tmp $SRCB/*
$JAVA -XX:AOTMode=record -XX:AOTCache=b.aot -XX:AOTCacheOutput=ba.aot com.sun.tools.javac.Main -d /tmp $SRCA/*

(

echo ==== no AOT ==============
perf stat -r $repeat $JAVA com.sun.tools.javac.Main -d /tmp $SRCA/SourceA1.java
perf stat -r $repeat $JAVA com.sun.tools.javac.Main -d /tmp $SRCB/SourceB1.java
echo =

for i in - +; do
    echo =
    if test "$i" == "-"; then
        extra="-XX:+UnlockDiagnosticVMOptions -XX:-AOTCodeCaching"
        echo ==== AOT cache, AOT code DISABLED ===========
    else
        extra=
        echo ==== AOT cache, AOT code ENABLED ============
    fi
    echo =
    perf stat -r $repeat $JAVA -XX:AOTCache=a.aot $extra com.sun.tools.javac.Main -d /tmp $SRCA/SourceA1.java
    perf stat -r $repeat $JAVA -XX:AOTCache=a.aot $extra com.sun.tools.javac.Main -d /tmp $SRCB/SourceB1.java

    echo =
    perf stat -r $repeat $JAVA -XX:AOTCache=b.aot $extra com.sun.tools.javac.Main -d /tmp $SRCA/SourceA1.java
    perf stat -r $repeat $JAVA -XX:AOTCache=b.aot $extra com.sun.tools.javac.Main -d /tmp $SRCB/SourceB1.java

    echo =
    perf stat -r $repeat $JAVA -XX:AOTCache=ab.aot $extra com.sun.tools.javac.Main -d /tmp $SRCA/SourceA1.java
    perf stat -r $repeat $JAVA -XX:AOTCache=ab.aot $extra com.sun.tools.javac.Main -d /tmp $SRCB/SourceB1.java

    echo =
    perf stat -r $repeat $JAVA -XX:AOTCache=ba.aot $extra com.sun.tools.javac.Main -d /tmp $SRCA/SourceA1.java
    perf stat -r $repeat $JAVA -XX:AOTCache=ba.aot $extra com.sun.tools.javac.Main -d /tmp $SRCB/SourceB1.java

done

) 2>&1 | tee log.txt


egrep '(elapsed)|(counter stat)|(=)' log.txt  | \
    sed -e "s/.*Perfor.*java /'java /g" \
        -e 's/ -XX:+UnlockDiagnosticVMOptions -XX:-AOTCodeCaching//g' \
        -e 's/^  */        /g' \
        -e 's/-d .tmp .tmp.aotbench.src..//g' \
        -e 's/^=$//g'

