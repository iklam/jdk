/*
 * Copyright (c) 2018, 2023, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

#ifndef SHARE_CDS_ARCHIVEHEAPLOADER_HPP
#define SHARE_CDS_ARCHIVEHEAPLOADER_HPP

#include "cds/filemap.hpp"
#include "cds/cds_globals.hpp"
#include "gc/shared/gc_globals.hpp"
#include "memory/allocation.hpp"
#include "memory/allStatic.hpp"
#include "memory/memRegion.hpp"
#include "oops/oopsHierarchy.hpp"
#include "runtime/globals.hpp"
#include "utilities/bitMap.hpp"
#include "utilities/macros.hpp"

class  FileMapInfo;
struct LoadedArchiveHeapRegion;

class ArchiveHeapLoader : AllStatic {
public:
  // Can this VM load the objects from archived heap region into the heap at start-up?
  static bool can_load()  NOT_CDS_JAVA_HEAP_RETURN_(false);

  static bool is_loaded() {
    CDS_JAVA_HEAP_ONLY(return _is_loaded;)
    NOT_CDS_JAVA_HEAP(return false;)
  }

  static void fixup_region() NOT_CDS_JAVA_HEAP_RETURN;

#if INCLUDE_CDS_JAVA_HEAP
private:
  static bool _is_loaded;

  static void new_fixup_region(TRAPS);
public:

  static bool load_heap_region(FileMapInfo* mapinfo);
  static bool new_load_heap_region(FileMapInfo* mapinfo);

#endif // INCLUDE_CDS_JAVA_HEAP

};

#endif // SHARE_CDS_ARCHIVEHEAPLOADER_HPP
