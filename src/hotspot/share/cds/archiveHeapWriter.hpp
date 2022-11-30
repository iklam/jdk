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

#include "cds/heapShared.hpp"
#include "memory/allocation.hpp"
#include "memory/allStatic.hpp"
#include "oops/oopHandle.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"
#include "utilities/resourceHash.hpp"

class MemRegion;
template<class E> class GrowableArray;
template <typename E, MEMFLAGS F> class GrowableArrayCHeap;

class ArchiveHeapWriter : AllStatic {
  class EmbeddedOopRelocator;

  // this->_buffer cannot contain more than this number of bytes.
  static constexpr int MAX_OUTPUT_BYTES = (int)max_jint;

  // The minimum region size of all collectors that are supported by CDS in
  // ArchiveHeapLoader::can_map() mode. Currently only G1 is supported. G1's region size
  // depends on -Xmx, but can never be smaller than 1 * M.
  static constexpr int MIN_GC_REGION_ALIGNMENT = 1 * M;

  // TODO: comments ...
  static OopHandle _buffer;

  // The exclusive end of the last object that has been copied into this->_buffer'
  static int _buffer_top;

  static GrowableArrayCHeap<u1, mtClassShared>* _output;

  // The exclusive top of the last object that has been copied into this->_output.
  static int _output_top;

  // The bounds of the open region inside this->_output.
  static int _open_bottom;  // inclusive
  static int _open_top;     // exclusive

  // The bounds of the closed region inside this->_output.
  static int _closed_bottom;  // inclusive
  static int _closed_top;     // exclusive

  // The bottom of the copy of Heap::roots() inside this->_output.
  static int _heap_roots_bottom;

  // TODO comment ...
  static address _requested_open_region_bottom;
  static address _requested_open_region_top;
  static address _requested_closed_region_bottom;
  static address _requested_closed_region_top;

  typedef ResourceHashtable<oop, int,
      36137, // prime number
      AnyObj::C_HEAP,
      mtClassShared,
      HeapShared::oop_hash> BufferedObjToOutputOffsetTable;
  static BufferedObjToOutputOffsetTable* _buffered_obj_to_output_offset_table;

  static int byte_size_of_buffered_obj(oop buffered_obj);
  static int cast_to_int_byte_size(size_t byte_size);

  static void copy_buffered_objs_to_output();
  static void copy_buffered_objs_to_output_by_region(bool copy_open_region);
  static int copy_one_buffered_obj_to_output(oop buffered_obj);
  static void set_requested_address_for_regions(GrowableArray<MemRegion>* closed_regions,
                                                GrowableArray<MemRegion>* open_regions);
  static void relocate_embedded_pointers_in_output();

  static bool is_in_requested_regions(oop o);
  static oop requested_obj_from_output_offset(int offset);
  static oop buffered_obj_to_requested_obj(oop buffered_obj);

  static void store_in_output(oop* p, oop output_referent);
  static void store_in_output(narrowOop* p, oop output_referent);

  template <typename T> static T* requested_addr_to_output_addr(T* p);

public:
  static void init(TRAPS) NOT_CDS_JAVA_HEAP_RETURN;
  static bool is_object_too_large(size_t size);
  static HeapWord* allocate_buffer_for(oop orig_obj);
  static HeapWord* allocate_raw_buffer(size_t size);
  static bool is_in_buffer(oop o);
  static void finalize(GrowableArray<MemRegion>* closed_regions, GrowableArray<MemRegion>* open_regions);
  static address heap_region_requested_bottom(int heap_region_idx);
  static oop heap_roots_requested_address();
  static oop requested_address_for_oop(oop orig_obj);
};

#endif // SHARE_CDS_ARCHIVEHEAPWRITER_HPP
