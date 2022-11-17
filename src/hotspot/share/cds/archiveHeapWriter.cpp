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

#include "precompiled.hpp"
#include "cds/archiveHeapWriter.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/universe.hpp"
#include "memory/oopFactory.hpp"
#include "oops/oopHandle.inline.hpp"
#include "oops/typeArrayOop.hpp"
#include "runtime/mutexLocker.hpp"

#if INCLUDE_CDS_JAVA_HEAP

OopHandle ArchiveHeapWriter::_buffer;
size_t ArchiveHeapWriter::_bytes_used = 0;
bool ArchiveHeapWriter::_copying_open_region_objects = false;

void ArchiveHeapWriter::init(TRAPS) {
  Universe::heap()->collect(GCCause::_java_lang_system_gc);
  size_t heap_used;
  {
    MonitorLocker ml(Heap_lock);
    heap_used = Universe::heap()->used();
  }

  size_t buffer_size = heap_used * 2;
  typeArrayOop buffer_oop = oopFactory::new_byteArray(buffer_size, CHECK);

  tty->print_cr("Heap used = " SIZE_FORMAT, heap_used);
  tty->print_cr("Max buffer size = " SIZE_FORMAT, buffer_size);
  tty->print_cr("Max buffer oop = " INTPTR_FORMAT, p2i(buffer_oop));

  _buffer = OopHandle(Universe::vm_global(), buffer_oop);
}

bool ArchiveHeapWriter::is_object_too_large(size_t size) {
  size_t min_g1_region = 1 * M; // FIXME - make consider other GCs as well
  assert(size * HeapWordSize > size, "no overflow");
  if (size * HeapWordSize > min_g1_region) {
    return true;
  } else {
    return false;
  }
}

HeapWord* ArchiveHeapWriter::allocate_buffer_for(oop orig_obj) {
  //assert(!copied...), "sanity");
  size_t size = orig_obj->size();
  return allocate_raw_buffer(size);
}

HeapWord* ArchiveHeapWriter::allocate_raw_buffer(size_t size) {
  typeArrayOop buffer_oop = (typeArrayOop)_buffer.resolve();
  jbyte* base =  buffer_oop->byte_at_addr(0);
  assert(is_aligned(base, HeapWordSize), "must be");
  base += _bytes_used;
  _bytes_used += size * HeapWordSize;
  size_t buffer_size = buffer_oop->length();
  if (_bytes_used > buffer_size) {
    assert(false, "We should have reserved enough buffer " SIZE_FORMAT " > " SIZE_FORMAT, _bytes_used, buffer_size);
    return NULL;
  }

  return (HeapWord*)base;
}


bool ArchiveHeapWriter::is_in_buffer(oop o) {
  typeArrayOop buffer_oop = (typeArrayOop)_buffer.resolve();
  jbyte* base =  buffer_oop->byte_at_addr(0);
  assert(is_aligned(base, HeapWordSize), "must be");

  jbyte* top = base + _bytes_used;

  return cast_to_oop(base) <= o && o < cast_to_oop(top);
}

// For the time being, always support two regions (to be strictly compatible with existing G1
// mapping code. We should eventually use a single region.
void ArchiveHeapWriter::finalize(GrowableArray<MemRegion>* closed_regions, GrowableArray<MemRegion>* open_regions) {

}

#endif // INCLUDE_CDS_JAVA_HEAP
