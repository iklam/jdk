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
#include "cds/heapShared.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/oopFactory.hpp"
#include "memory/universe.hpp"
#include "oops/compressedOops.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oopHandle.inline.hpp"
#include "oops/typeArrayOop.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/growableArray.hpp"

#if INCLUDE_G1GC
#include "gc/g1/g1CollectedHeap.hpp"
#include "gc/g1/heapRegion.hpp"
#endif

#if INCLUDE_CDS_JAVA_HEAP

// buffer
OopHandle ArchiveHeapWriter::_buffer;
int ArchiveHeapWriter::_buffer_top;

// output
GrowableArrayCHeap<u1, mtClassShared>* ArchiveHeapWriter::_output;
int ArchiveHeapWriter::_output_top;
int ArchiveHeapWriter::_open_bottom;
int ArchiveHeapWriter::_open_top;
int ArchiveHeapWriter::_closed_bottom;
int ArchiveHeapWriter::_closed_top;

address ArchiveHeapWriter::_requested_open_region_bottom;
address ArchiveHeapWriter::_requested_open_region_top;
address ArchiveHeapWriter::_requested_closed_region_bottom;
address ArchiveHeapWriter::_requested_closed_region_top;

ArchiveHeapWriter::BufferedObjToOutputOffsetTable*
  ArchiveHeapWriter::_buffered_obj_to_output_offset_table = NULL;

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
  _buffer_top = 0;

  _buffered_obj_to_output_offset_table = new BufferedObjToOutputOffsetTable();

  _requested_open_region_bottom = NULL;
  _requested_open_region_top = NULL;
  _requested_closed_region_bottom = NULL;
  _requested_closed_region_top = NULL;
}

bool ArchiveHeapWriter::is_object_too_large(size_t size) {
  assert(size > 0, "no zero-size object");
  assert(size * HeapWordSize > size, "no overflow");
  static_assert(MIN_GC_REGION_ALIGNMENT > 0, "must be positive");

  size_t byte_size = size * HeapWordSize;
  if (byte_size > size_t(MIN_GC_REGION_ALIGNMENT)) {
    return true;
  } else {
    return false;
  }
}

int ArchiveHeapWriter::cast_to_int_byte_size(size_t byte_size) {
  assert(byte_size <= size_t(MIN_GC_REGION_ALIGNMENT), "must be");
  static_assert(MIN_GC_REGION_ALIGNMENT < max_jint, "must be");
  return (int)(byte_size);
}

int ArchiveHeapWriter::byte_size_of_buffered_obj(oop buffered_obj) {
  assert(!is_object_too_large(buffered_obj->size()), "sanity");
  return cast_to_int_byte_size(buffered_obj->size() * HeapWordSize);
}

HeapWord* ArchiveHeapWriter::allocate_buffer_for(oop orig_obj) {
  size_t size = orig_obj->size();
  return allocate_raw_buffer(size);
}

HeapWord* ArchiveHeapWriter::allocate_raw_buffer(size_t size) {
  assert(size > 0, "no zero-size object");
  assert(size * HeapWordSize > size, "no overflow");

  static_assert(MIN_GC_REGION_ALIGNMENT < max_jint, "sanity");
  size_t byte_size = size * HeapWordSize;
  assert(byte_size < MIN_GC_REGION_ALIGNMENT, "should have been checked");
  assert(byte_size < max_jint, "sanity");

  typeArrayOop buffer_oop = (typeArrayOop)_buffer.resolve();
  int buffer_size = buffer_oop->length();

  int new_top = _buffer_top + (int)(byte_size);
  assert(new_top > _buffer_top, "no wrap around; no zero-size object");
  assert(new_top <= buffer_size, "We should have reserved enough buffer: newtop = %d, buffer_size = %d", new_top, buffer_size);

  jbyte* base =  buffer_oop->byte_at_addr(0);
  assert(is_aligned(base, HeapWordSize), "must be");

  jbyte* allocated = base + _buffer_top;
  _buffer_top = new_top;

  return (HeapWord*)allocated;
}

bool ArchiveHeapWriter::is_in_buffer(oop o) {
  typeArrayOop buffer_oop = (typeArrayOop)_buffer.resolve();
  jbyte* base =  buffer_oop->byte_at_addr(0);
  assert(is_aligned(base, HeapWordSize), "must be");

  jbyte* top = base + _buffer_top;

  return cast_to_oop(base) <= o && o < cast_to_oop(top);
}

bool ArchiveHeapWriter::is_in_requested_regions(oop o) {
  assert(_requested_open_region_bottom != NULL, "do not call before this is initialized");
  assert(_requested_closed_region_bottom != NULL, "do not call before this is initialized");

  address a = cast_from_oop<address>(o);
  return (_requested_open_region_bottom <= a && a < _requested_open_region_top) ||
         (_requested_closed_region_bottom <= a && a < _requested_closed_region_top);
}

oop ArchiveHeapWriter::oop_from_output_offset(int offset) {
  oop o = cast_to_oop(_requested_open_region_bottom + offset);
  assert(is_in_requested_regions(o), "must be");
  return o;
}

// For the time being, always support two regions (to be strictly compatible with existing G1
// mapping code. We should eventually use a single region.
void ArchiveHeapWriter::finalize(GrowableArray<MemRegion>* closed_regions, GrowableArray<MemRegion>* open_regions) {
  copy_buffered_objs_to_output();
  set_requested_address_for_regions(closed_regions, open_regions);
  relocate_embedded_pointers_in_output();
}

void ArchiveHeapWriter::copy_buffered_objs_to_output() {
  int initial_buffer_size = _buffer_top;
  DEBUG_ONLY(initial_buffer_size = MAX2(10000, initial_buffer_size / 10)); // test for expansion logic

  _output = new GrowableArrayCHeap<u1, mtClassShared>(initial_buffer_size);

  _output_top = _open_bottom = 0;
  for (int i = 0; i < 2; i ++) {
    // We copy the objects for the open region first, so that the end of the closed region
    // aligns with the end of the heap.
    //
    // TODO: ascii art
    bool copy_open_region = (i == 0) ? true : false;
    copy_buffered_objs_to_output_by_region(copy_open_region);
    if (i == 0) {
      copy_one_buffered_obj_to_output(HeapShared::roots()); // this is not in HeapShared::archived_object_cache()
      _open_top = _output_top;
      _output_top = _closed_bottom = align_up(_output_top, HeapRegion::GrainBytes);
    }
  }
  _closed_top = _output_top;

  tty->print_cr("Size of open region   = %d bytes", _open_top   - _open_bottom);
  tty->print_cr("Size of closed region = %d bytes", _closed_top - _closed_bottom);
}

void ArchiveHeapWriter::copy_buffered_objs_to_output_by_region(bool copy_open_region) {
  auto copier = [&] (oop orig_obj, HeapShared::CachedOopInfo& info) {
    if (info.in_open_region() == copy_open_region) {
      // For region-based collectors such as G1, we need to make sure that we don't have
      // an object that can possible span across two regions.
      int output_offset = copy_one_buffered_obj_to_output(info.archived_obj());
      info.set_output_offset(output_offset);

      bool is_new = _buffered_obj_to_output_offset_table->put(info.buffered_obj(), output_offset);
      assert(is_new, "sanity");
    }
  };
  HeapShared::archived_object_cache()->iterate_all(copier);
}

int ArchiveHeapWriter::copy_one_buffered_obj_to_output(oop buffered_obj) {
  assert(is_in_buffer(buffered_obj), "sanity");
  int byte_size = byte_size_of_buffered_obj(buffered_obj);
  assert(byte_size > 0, "no zero-size objects");
  int new_top = _output_top + byte_size;
  assert(new_top > _output_top, "no wrap around");

  int cur_min_region_bottom = align_down(_output_top, MIN_GC_REGION_ALIGNMENT);
  int next_min_region_bottom = align_down(new_top, MIN_GC_REGION_ALIGNMENT);
  if (cur_min_region_bottom != next_min_region_bottom) {
    assert(next_min_region_bottom > cur_min_region_bottom, "must be");
    assert(next_min_region_bottom - cur_min_region_bottom == MIN_GC_REGION_ALIGNMENT, "no buffered object can be larger than %d bytes",
           MIN_GC_REGION_ALIGNMENT);
    Unimplemented();
  }

  tty->print_cr("%p = @%d", buffered_obj, _output_top);
  while (_output->length() < new_top) {
    _output->append(0); // FIXME -- grow in blocks!
  }

  u1* from = cast_from_oop<u1*>(buffered_obj);
  u1* to = _output->adr_at(_output_top);
  assert(is_aligned(_output_top, HeapWordSize), "sanity");
  assert(is_aligned(byte_size, HeapWordSize), "sanity");
  memcpy(to, from, byte_size);

  int output_offset = _output_top;
  _output_top = new_top;

  return output_offset;
}

void ArchiveHeapWriter::set_requested_address_for_regions(GrowableArray<MemRegion>* closed_regions,
                                                          GrowableArray<MemRegion>* open_regions) {
  assert(closed_regions->length() == 0, "must be");
  assert(open_regions->length() == 0, "must be");

  assert(UseG1GC, "must be");
  address heap_end = (address)G1CollectedHeap::heap()->reserved().end();
  tty->print_cr("Heap end = %p", heap_end);

  int closed_region_byte_size = _closed_top - _closed_bottom;
  int open_region_byte_size = _open_top - _open_bottom;
  assert(closed_region_byte_size > 0, "must archived at least one object for closed region!");
  assert(open_region_byte_size > 0, "must archived at least one object for open region!");

  // The following two asserts are ensured by copy_buffered_objs_to_output_by_region().
  assert(is_aligned(_closed_bottom, HeapRegion::GrainBytes), "sanity");
  assert(is_aligned(_open_bottom, HeapRegion::GrainBytes), "sanity");

  _requested_closed_region_bottom = align_down(heap_end - closed_region_byte_size, HeapRegion::GrainBytes);
  _requested_open_region_bottom = _requested_closed_region_bottom - (_closed_bottom - _open_bottom);

  assert(is_aligned(_requested_closed_region_bottom, HeapRegion::GrainBytes), "sanity");
  assert(is_aligned(_requested_open_region_bottom, HeapRegion::GrainBytes), "sanity");

  _requested_open_region_top = _requested_open_region_bottom + (_open_top - _open_bottom);
  _requested_closed_region_top = _requested_closed_region_bottom + (_closed_top - _closed_bottom);

  assert(_requested_open_region_top <= _requested_closed_region_bottom, "no overlap");

  tty->print_cr("Requested open region %p",   _requested_open_region_bottom);
  tty->print_cr("Requested closed region %p", _requested_closed_region_bottom);
}

oop ArchiveHeapWriter::buffered_obj_to_output_obj(oop buffered_obj) {
  assert(is_in_buffer(buffered_obj), "Hah!");
  int* p = _buffered_obj_to_output_offset_table->get(buffered_obj);
  assert(p != NULL, "must have copied " INTPTR_FORMAT " to output", p2i(buffered_obj));
  int output_offset = *p;
  oop output_obj = oop_from_output_offset(output_offset);

  return output_obj;
}

class ArchiveHeapWriter::EmbeddedOopRelocator: public BasicOopIterateClosure {
  oop _buffered_obj;
  oop _output_obj;

public:
  EmbeddedOopRelocator(oop buffered_obj, oop output_obj) :
    _buffered_obj(buffered_obj), _output_obj(output_obj) {}

  void do_oop(narrowOop *p) { EmbeddedOopRelocator::do_oop_work(p); }
  void do_oop(      oop *p) { EmbeddedOopRelocator::do_oop_work(p); }

private:
  template <class T> void do_oop_work(T *p) {
    oop buffered_referent = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(buffered_referent)) {
      oop output_referent = buffered_obj_to_output_obj(buffered_referent);
      tty->print_cr("Relocate %p => %p", buffered_referent, output_referent);

      size_t field_offset = pointer_delta(p, _buffered_obj, sizeof(char));

      T* new_p = (T*)(cast_from_oop<address>(_output_obj) + field_offset);

      ArchiveHeapWriter::store_in_output(new_p, output_referent);
    }
  }
};


template <typename T> T* ArchiveHeapWriter::requested_addr_to_output_addr(T* p) {
  assert(is_in_requested_regions(cast_to_oop(p)), "must be");

  address addr = address(p);
  assert(addr >= _requested_open_region_bottom, "must be");
  size_t offset = addr - _requested_open_region_bottom;
  address output_addr = address(_output->adr_at(offset));
  return (T*)(output_addr);
}

void ArchiveHeapWriter::store_in_output(oop* p, oop output_referent) {
  oop* addr = requested_addr_to_output_addr(p);
  *addr = output_referent;
}

void ArchiveHeapWriter::store_in_output(narrowOop* p, oop output_referent) {
  narrowOop val = CompressedOops::encode_not_null(output_referent);
  narrowOop* addr = requested_addr_to_output_addr(p);
  *addr = val;
}

void ArchiveHeapWriter::relocate_embedded_pointers_in_output() {
  auto iterator = [&] (oop orig_obj, HeapShared::CachedOopInfo& info) {
    oop buffered_obj = info.buffered_obj();
    oop output_obj = oop_from_output_offset(info.output_offset());
    EmbeddedOopRelocator relocator(buffered_obj, output_obj);
    info.buffered_obj()->oop_iterate(&relocator);    
  };
  HeapShared::archived_object_cache()->iterate_all(iterator);
}

#endif // INCLUDE_CDS_JAVA_HEAP
