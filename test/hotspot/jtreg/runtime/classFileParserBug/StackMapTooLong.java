/*
 * Copyright (c) 2026, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/*
 * @test Very large StackMapTable should cause OutOfMemoryError and not VM crash.
 * @bug 8386562
 * @library /test/lib
 * @run main StackMapTooLong
 */

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.URL;
import java.net.URLClassLoader;
import jdk.test.lib.helpers.ClassFileAssembler;

public class StackMapTooLong {
    public static void main(String args[]) throws Throwable {
        String jcodFile = "BadClass.jcod";
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(jcodFile))) {
            writer.write(head);
            int max = 2100000;
            for (int i = 0; i < max; i++) {
                writer.write("0x0000000000000000;\n");
            }
            writer.write(tail);
        }

        // Generate a StackMapTable that's larger than to Metaspace::max_allocation_word_size()
        ClassFileAssembler.compileJcod(jcodFile);

        URL url = new File(System.getProperty("user.dir")).toURI().toURL();
        URL[] urls = new URL[] {url};
        URLClassLoader loader = new URLClassLoader(urls);
        try {
            loader.loadClass("BadClass");
            throw new RuntimeException("OutOfMemoryError expected but not thrown!");
        } catch (OutOfMemoryError expected) {}
    }

    static final String head = """
// This file is auto-generated
class BadClass {
  0xCAFEBABE;
  0; // minor version
  52; // version
  [] { // Constant Pool
    ; // first element is empty
    Utf8 "BadClass"; // #1
    class #1; // #2
    Utf8 "java/lang/Object"; // #3
    class #3; // #4
    Utf8 "<init>"; // #5
    Utf8 "()V"; // #6
    NameAndType #5 #6; // #7
    Method #4 #7; // #8
    Utf8 "java/lang/Throwable"; // #9
    class #9; // #10
    Utf8 "Code"; // #11
    Utf8 "StackMapTable"; // #12
  } // Constant Pool

  0x0001; // access
  #2;// this_cpx
  #4;// super_cpx

  [] { // Interfaces
  } // Interfaces

  [] { // Fields
  } // Fields

  [] { // Methods
    {  // method
      0x0001; // access
      #5; // name_index
      #6; // descriptor_index
      [] { // Attributes
        Attr(#11) { // Code
          1; // max_stack
          1; // max_locals
          Bytes[]{
            0xB70008B100000000;
          }
          [] { // Traps
          } // end Traps
          [] { // Attributes
            Attr(#12) { // StackMapTable
              [] { // -- to be followed by *a lot* of zeros
""";

    static final String tail = """
              }
            } // end StackMapTable
          } // Attributes
        } // end Code
      } // Attributes
    }
  } // Methods

  [] { // Attributes
  } // Attributes
} // end class BadClass
""";

}
