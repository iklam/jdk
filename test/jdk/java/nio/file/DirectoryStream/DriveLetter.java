/*
 * Copyright (c) 2008, 2025, Oracle and/or its affiliates. All rights reserved.
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

/* @test
 * @bug 6808647
 * @summary Checks that a DirectoryStream's iterator returns the expected
 *    path when opening a directory by specifying only the drive letter.
 * @requires (os.family == "windows")
 * @library ..
 */

import java.nio.file.*;
import java.io.File;
import java.io.IOException;

public class DriveLetter {

    public static void main(String[] args) throws IOException {
        String here = System.getProperty("user.dir");
        if (here.length() < 2 || here.charAt(1) != ':')
            throw new RuntimeException("Unable to determine drive letter");

        // create temporary file in current directory
        File tempFile = File.createTempFile("foo", "tmp", new File(here));
        try {
            // we should expect C:foo.tmp to be returned by iterator
            String drive = here.substring(0, 2);
            Path expected = Paths.get(drive).resolve(tempFile.getName());

            boolean found = false;
            Path dir = Paths.get(drive);
            try (DirectoryStream<Path> stream = Files.newDirectoryStream(dir)) {
                for (Path file : stream) {
                    if (file.equals(expected)) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
                throw new RuntimeException("Temporary file not found???");

        } finally {
            tempFile.delete();
        }
    }
}
