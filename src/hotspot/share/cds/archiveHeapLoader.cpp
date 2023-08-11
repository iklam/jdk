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

#include "precompiled.hpp"
#include "cds/filemap.hpp"
#include "cds/archiveHeapLoader.hpp"
#include "cds/heapShared.hpp"
#include "cds/metaspaceShared.hpp"
#include "classfile/classLoaderDataShared.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "logging/log.hpp"
#include "runtime/java.hpp"
#include "memory/iterator.inline.hpp"
#include "utilities/bitMap.inline.hpp"

#if INCLUDE_CDS_JAVA_HEAP
bool ArchiveHeapLoader::_is_loaded = false;

bool ArchiveHeapLoader::can_load() {
  if (MinHeapSize < 8 * M) {
    // This may trigger early GC, leading to VM exit.
    // TODO: is a more precise check possible?
    // TODO: need to check how much heap is actually needed by CDS.
    return false;
  }

  // TODO -- enable can_load_archived_objects() for ZGC and Shenandoah after testing.
  // Eventually loading will be supporter on all GCs and this API will be removed.
  return Universe::heap()->can_load_archived_objects();
}

static size_t _new_load_heap_size; // total size of heap region, in number of HeapWords
static char* _new_load_heap_buff;

// Algorithm
//
// - Input: objects inside [_stream_bottom ... _stream_top). These objects are laid out
//   contiguously.
//
// - First, copy each input object into its "materialized" address in the heap. The
//   materialized objects are usually contiguous, but could be divided into a few
//   disjoint blocks stored in _allocated_blocks.
// - When each object is copied, any embedded native pointers are relocated.
// - After the object is copied, its materialized address is written into the first word
//   of the "stream" copy.
//
// - We then iterate over each block in _allocated_block, relocating all oop pointers
//   that are marked by the oopmap. Relocation is done by first finding the "stream"
//   copy of the pointee, where we can read the materialized of the pointee.
//
// To understand this code, you should trace it in gdb while referencing the CDS map file
// created with "java -Xshare:dump -Xlog:cds+map*=trace:file=cds.map:none:filesize=0"
template <bool COOPS>
class ArchiveHeapLoaderImpl : public StackObj {
  // Input stream of the archive objects.
  HeapWord* _stream_bottom;
  HeapWord* _stream_top;

  // bitmap for oop relocation
  BitMapView _oopmap;

  // bitmap for native pointer relocation
  BitMapView _ptrmap;
  BitMap::idx_t _next_native_ptr_idx;
  HeapWord* _next_native_ptr_in_stream;

  struct Block {
    HeapWord* _bottom;
    HeapWord* _top;
    Block(HeapWord* b, HeapWord* t) : _bottom(b), _top(t) {}
    Block() : _bottom(nullptr), _top(nullptr) {}
  };

  // The archived objects may be materialized in one or more blocks.
  GrowableArray<Block> _allocated_blocks;
  HeapWord* _last_block_bottom;
  HeapWord* _last_oop_top;
  DEBUG_ONLY(oop _lowest_materialized_oop;)
  DEBUG_ONLY(oop _highest_materialized_oop;)

public:
  inline ArchiveHeapLoaderImpl(char* stream, size_t bytesize) {
    assert(COOPS == UseCompressedOops, "sanity");
    _stream_bottom = (HeapWord*)stream;
    _stream_top    = _stream_bottom + bytesize / HeapWordSize;

    _last_block_bottom = nullptr;
    _last_oop_top = nullptr;
    init_oopmap();
    init_ptrmap();

    DEBUG_ONLY(_lowest_materialized_oop = nullptr);
    DEBUG_ONLY(_highest_materialized_oop = nullptr);
  }

  inline oop load_archive_heap(TRAPS) {
    copy_objects(_stream_bottom, _stream_top, CHECK_NULL);
    relocate_oop_pointers();

    // HeapShared::roots() is at this offset in the stream.
    size_t heap_roots_stream_offset = FileMapInfo::current_info()->heap_roots_offset() / HeapWordSize;

    // The materialized address of the HeapShared::roots()
    return *(oop*)(_stream_bottom + heap_roots_stream_offset);
  }

private:
  inline void copy_objects(HeapWord* stream, HeapWord* stream_top, TRAPS) {
    while (stream < stream_top) {
      size_t size; // size of object being copied
      oop m = allocate(stream, size, CHECK);
      HeapWord* obj_bottom = cast_from_oop<HeapWord*>(m);
      if (_last_oop_top != obj_bottom) {
        add_new_block(obj_bottom);
      }
      _last_oop_top = obj_bottom + size;

      DEBUG_ONLY(_lowest_materialized_oop = MIN2(_lowest_materialized_oop, m));
      DEBUG_ONLY(_highest_materialized_oop = MAX2(_highest_materialized_oop, m));
      memcpy(obj_bottom, stream, size * HeapWordSize);

      // Relocate native pointers, if necessary.
      HeapWord* stream_next = stream + size;
      while (stream_next > _next_native_ptr_in_stream) {
        assert(stream < _next_native_ptr_in_stream, "must be in the current object, and cannot be first word");
        relocate_one_native_pointer(stream, obj_bottom);
      }

      // We don't use the content of this object in the stream anymore, use this space
      // to store the materialized address, to be used by relocation.
      *(oop*)stream = m;

      stream = stream_next;
    }
    add_new_block(nullptr); // catch the last block
  }

  inline oop allocate(HeapWord* stream, size_t& size, TRAPS) {
    oop o = cast_to_oop(stream); // "original" from the stream
    size = o->size();

    assert(!o->is_instanceRef(), "no such objects are archived");
    assert(!o->is_stackChunk(), "no such objects are archived");

    if (o->is_instance()) {
      return Universe::heap()->obj_allocate(o->klass(), size, CHECK_NULL);
      // Can't use the following because o->klass() isn't initialized (so injected field sizes aren't known??)
      // m = InstanceKlass::cast(o->klass())->allocate_instance(CHECK);
    } else if (o->is_typeArray()) {
      int len = static_cast<typeArrayOop>(o)->length();
      return TypeArrayKlass::cast(o->klass())->allocate(len, CHECK_NULL);
    } else {
      assert(o->is_objArray(), "must be");
      int len = static_cast<objArrayOop>(o)->length();
      return ObjArrayKlass::cast(o->klass())->allocate(len, CHECK_NULL);
    }
  }

  NOINLINE void add_new_block(HeapWord* new_obj) {
    if (_last_block_bottom) {
      _allocated_blocks.append(Block(_last_block_bottom, _last_oop_top));
    }
    _last_block_bottom = new_obj;
  }

  template <typename T>
  class OopPatcherBase : public BitMapClosure {
  protected:
    ArchiveHeapLoaderImpl<COOPS>* _loader;
    T* _base;
    address _stream_bottom;
  public:
    OopPatcherBase(ArchiveHeapLoaderImpl<COOPS>* loader, T* base) : _loader(loader), _base(base) {
      _stream_bottom = (address)_loader->_stream_bottom;
    }

    inline void patch(T* p, size_t pointee_byte_offset) {
      // This is the adddress of the pointee inside the input stream
      address pointee_stream_header_addr = _stream_bottom + pointee_byte_offset;

      // The materialized address of this pointer is stored there.
      oop materialized_pointee = *(oop*)pointee_stream_header_addr;

      assert(materialized_pointee >= _loader->_lowest_materialized_oop &&
             materialized_pointee <= _loader->_highest_materialized_oop, "sanity");
      HeapAccess<IS_NOT_NULL>::oop_store(p, materialized_pointee);
      //tty->print_cr("Relocated " SIZE_FORMAT_W(7) " => %p", pointee_byte_offset, materialized_pointee);
    }
  };

  template <int DUMPTIME_SHIFT>
  class NarrowOopPatcher: OopPatcherBase<narrowOop> {
    // The requested address of the lowest archived object is encoded as this narrowOop
    narrowOop _lowest_requested_narrowOop;
  public:
    NarrowOopPatcher(ArchiveHeapLoaderImpl<COOPS>* loader, narrowOop* base) : OopPatcherBase<narrowOop>(loader, base) {
      size_t n = FileMapInfo::current_info()->region_at(MetaspaceShared::hp)->mapping_offset() >> DUMPTIME_SHIFT;
      assert(n <= 0xffffffff, "must be");
      _lowest_requested_narrowOop = (narrowOop)n;
    }

    bool do_bit(size_t offset) {
      narrowOop* p = OopPatcherBase<narrowOop>::_base + offset;
      narrowOop narrow = *p;
      assert(narrow != narrowOop::null, "must be");
      // The pointee is at this byte offset from the lowest archived object
      assert(narrow >= _lowest_requested_narrowOop, "must be");
      size_t pointee_byte_offset = (size_t(narrow) - size_t(_lowest_requested_narrowOop)) << DUMPTIME_SHIFT;
      OopPatcherBase<narrowOop>::patch(p, pointee_byte_offset);
      return true;
    }
  };

  class OopPatcher: OopPatcherBase<oop> {
    oop _lowest_requested_oop; // Requested address of the lowest archived object
  public:
    OopPatcher(ArchiveHeapLoaderImpl<COOPS>* loader, oop* base) : OopPatcherBase<oop>(loader, base) {
      _lowest_requested_oop = cast_to_oop(FileMapInfo::current_info()->heap_region_requested_address());
    }
    bool do_bit(size_t offset) {
      oop* p = OopPatcherBase<oop>::_base + offset;
      oop o = *p;
      assert(o != nullptr, "must be");
      // The pointee is at this byte offset from the lowest archived object
      assert(o >= _lowest_requested_oop, "must be");
      size_t pointee_byte_offset = cast_from_oop<address>(o) - cast_from_oop<address>(_lowest_requested_oop);
      OopPatcherBase<oop>::patch(p, pointee_byte_offset);
      return true;
    }
  };

  void relocate_oop_pointers() {
    const int len = _allocated_blocks.length();
    size_t done_size = 0; // number of allocated words that have been processed so far
    size_t scale = COOPS ? sizeof(HeapWord) / sizeof(narrowOop) : 1;

    // We know there are no set bits below lowest_bit.
    BitMap::idx_t lowest_bit = (BitMap::idx_t)FileMapInfo::current_info()->heap_oopmap_leading_zeros();

    for (int i = 0; i < len; i++) {
      // relocate all pointers in [bottom .. top)
      HeapWord* bottom = _allocated_blocks.adr_at(i)->_bottom;
      HeapWord* top = _allocated_blocks.adr_at(i)->_top;
      size_t size = pointer_delta(top, bottom, sizeof(HeapWord)); // word size of current block
      log_info(cds)("Relocating oops in block %d: [" INTPTR_FORMAT " - " INTPTR_FORMAT
                    "] (" SIZE_FORMAT_W(7) ") bytes", i, p2i(bottom), p2i(top), size * HeapWordSize);

      // Number of bits covered by this Block. For COOPS, each HeapWord contains two narrowOops.
      BitMap::idx_t start_bit = (BitMap::idx_t)(done_size * scale);
      BitMap::idx_t num_bits = (BitMap::idx_t)(size * scale);
      BitMap::idx_t end_bit = start_bit + num_bits;

      if (start_bit < lowest_bit) {
        start_bit = lowest_bit;
      }
      if (start_bit < end_bit) {
        address base = address(bottom) - done_size * HeapWordSize;
        if (COOPS) {
          int dumptime_oop_shift = FileMapInfo::current_info()->narrow_oop_shift();
          assert(dumptime_oop_shift <= 3, "other values are not supproted by the C++ templates");
          if (dumptime_oop_shift == 0) {
            NarrowOopPatcher<0> patcher(this, (narrowOop*)base);
            _oopmap.iterate(&patcher, start_bit, end_bit);
          } else if (dumptime_oop_shift == 1) {
            NarrowOopPatcher<1> patcher(this, (narrowOop*)base);
            _oopmap.iterate(&patcher, start_bit, end_bit);
          } else if (dumptime_oop_shift == 2) {
            NarrowOopPatcher<2> patcher(this, (narrowOop*)base);
            _oopmap.iterate(&patcher, start_bit, end_bit);
          } else {
            assert(dumptime_oop_shift == 3, "sanity");
            NarrowOopPatcher<3> patcher(this, (narrowOop*)base);
            _oopmap.iterate(&patcher, start_bit, end_bit);
          }
        } else {
          OopPatcher patcher(this, (oop*)base);
          _oopmap.iterate(&patcher, start_bit, end_bit);
        }
      }

      done_size += size;
    }
  }

  void init_oopmap() {
    FileMapInfo::current_info()->map_bitmap_region();
    FileMapRegion* heap_region = FileMapInfo::current_info()->region_at(MetaspaceShared::hp);
    FileMapRegion* bitmap_region = FileMapInfo::current_info()->region_at(MetaspaceShared::bm);

    address start = (address)(bitmap_region->mapped_base()) + heap_region->oopmap_offset();
    _oopmap = BitMapView((BitMap::bm_word_t*)start, heap_region->oopmap_size_in_bits());
  }

  void init_ptrmap() {
    _next_native_ptr_in_stream = _stream_top;
    _next_native_ptr_idx = 0;

    if (MetaspaceShared::relocation_delta() == 0) {
      return;
    }

    FileMapRegion* r = FileMapInfo::current_info()->region_at(MetaspaceShared::hp);
    if (!r->has_ptrmap()) {
      return;
    }

    _ptrmap = r->ptrmap_view();
    update_next_native_ptr_in_stream(0);
  }

  inline void update_next_native_ptr_in_stream(BitMap::idx_t increment) {
    _next_native_ptr_idx += increment;
    _next_native_ptr_idx = _ptrmap.find_first_set_bit(_next_native_ptr_idx);
    if (_next_native_ptr_idx < _ptrmap.size()) {
      _next_native_ptr_in_stream = _stream_bottom + _next_native_ptr_idx;
    } else {
      // we have relocated all native pointers
      _next_native_ptr_in_stream = _stream_top;
    }
  }

  inline void relocate_one_native_pointer(HeapWord* stream, HeapWord* m) {
    assert(_stream_bottom < _next_native_ptr_in_stream && _next_native_ptr_in_stream < _stream_top, "must be");
    size_t offset = pointer_delta(_next_native_ptr_in_stream, stream, sizeof(HeapWord));
    address* src_loc = (address*)_next_native_ptr_in_stream;
    address* dst_loc = (address*)(m + offset);
    address requested_ptr = *src_loc;
    address relocated_ptr = requested_ptr + MetaspaceShared::relocation_delta();

    // Currently we have only Klass pointers in heap objects.
    // This needs to be relaxed when we support other types of native
    // pointers such as Method.
    assert(((Klass*)(relocated_ptr))->is_klass(), "must be");
    *dst_loc = relocated_ptr;

    update_next_native_ptr_in_stream(1);
  }
}; // ArchiveHeapLoaderImpl


bool ArchiveHeapLoader::load_heap_region(char* stream, size_t bytesize) {
  JavaThread* THREAD = JavaThread::current();
  ResourceMark rm(THREAD);
  jlong time_started = os::thread_cpu_time(THREAD);
  jlong time_done;
  oop roots;

  if (UseCompressedOops) {
    ArchiveHeapLoaderImpl<true> loader(stream, bytesize);
    roots = loader.load_archive_heap(THREAD);
  } else {
    ArchiveHeapLoaderImpl<false> loader(stream, bytesize);
    roots = loader.load_archive_heap(THREAD);
  }
  if (HAS_PENDING_EXCEPTION) {
    // We cannot continue, as some of the materialized objects will have unrelocated
    // oop pointers. There's no point trying to recover. The heap is too small to do
    // anything anyway.
    vm_exit_during_initialization("Cannot load archived heap. "
                                  "Initial heap size too small.");

  }
  _is_loaded = true;
  HeapShared::init_roots(roots);

  if (!MetaspaceShared::use_full_module_graph()) {
    // Need to remove all the archived java.lang.Module objects from HeapShared::roots().
    ClassLoaderDataShared::clear_archived_oops();
  }


  log_info(cds)("Finished heap loading: roots = " INTPTR_FORMAT, p2i(roots));
  time_done = os::thread_cpu_time(THREAD);
  log_info(cds, gc)("Load Time: " JLONG_FORMAT, (time_done - time_started));

  return is_loaded();
}
#endif // INCLUDE_CDS_JAVA_HEAP
