/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_ARCHIVEHEAPWRITER_HPP
#define SHARE_CDS_ARCHIVEHEAPWRITER_HPP

#include "memory/allStatic.hpp"
#include "oops/oopHandle.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"

class MemRegion;
template<class E> class GrowableArray;

class ArchiveHeapWriter : AllStatic {
  static OopHandle _buffer;
  static size_t _bytes_used;
  static bool _copying_open_region_objects;

public:
  static void init(TRAPS) NOT_CDS_JAVA_HEAP_RETURN;
  static bool is_object_too_large(size_t size);
  static HeapWord* allocate_buffer_for(oop orig_obj);
  static HeapWord* allocate_raw_buffer(size_t size);
  static bool is_in_buffer(oop o);
  static void start_open_region_objects() {
    _copying_open_region_objects = true;
  }
  static void finalize(GrowableArray<MemRegion>* closed_regions, GrowableArray<MemRegion>* open_regions);
};

#endif // SHARE_CDS_ARCHIVEHEAPWRITER_HPP
