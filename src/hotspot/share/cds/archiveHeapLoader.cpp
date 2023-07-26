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
#include "cds/archiveHeapLoader.inline.hpp"
#include "cds/archiveHeapWriter.hpp"
#include "cds/heapShared.hpp"
#include "cds/metaspaceShared.hpp"
#include "classfile/classLoaderDataShared.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "logging/log.hpp"
#include "runtime/java.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/copy.hpp"

#if INCLUDE_CDS_JAVA_HEAP

bool ArchiveHeapLoader::_is_mapped = false;
bool ArchiveHeapLoader::_is_loaded = false;

bool    ArchiveHeapLoader::_narrow_oop_base_initialized = false;
address ArchiveHeapLoader::_narrow_oop_base;
int     ArchiveHeapLoader::_narrow_oop_shift;

// Support for loaded heap.
uintptr_t ArchiveHeapLoader::_loaded_heap_bottom = 0;
uintptr_t ArchiveHeapLoader::_loaded_heap_top = 0;
uintptr_t ArchiveHeapLoader::_dumptime_base = UINTPTR_MAX;
uintptr_t ArchiveHeapLoader::_dumptime_top = 0;
intx ArchiveHeapLoader::_runtime_offset = 0;
bool ArchiveHeapLoader::_loading_failed = false;

// Support for mapped heap.
uintptr_t ArchiveHeapLoader::_mapped_heap_bottom = 0;
bool      ArchiveHeapLoader::_mapped_heap_relocation_initialized = false;
ptrdiff_t ArchiveHeapLoader::_mapped_heap_delta = 0;


void ArchiveHeapLoader::fixup_region() {
  JavaThread* THREAD = JavaThread::current();
  new_fixup_region(THREAD);
  if (HAS_PENDING_EXCEPTION) {
    // We cannot continue, as some of the materialized objects will have unrelocated
    // oop pointers. There's no point trying to recover. The heap is too small to do
    // anything anyway.
    vm_exit_during_initialization("Cannot load archived heap. "
                                  "Initial heap size too small.");

  }
  if (!_is_loaded) {
    MetaspaceShared::disable_full_module_graph();
  }

  if (is_in_use()) {
    if (!MetaspaceShared::use_full_module_graph()) {
      // Need to remove all the archived java.lang.Module objects from HeapShared::roots().
      ClassLoaderDataShared::clear_archived_oops();
    }
  }
}

bool ArchiveHeapLoader::can_load() {
  return true;
}

bool ArchiveHeapLoader::load_heap_region(FileMapInfo* mapinfo) {
  return new_load_heap_region(mapinfo);
}

class VerifyLoadedHeapEmbeddedPointers: public BasicOopIterateClosure {
  ResourceHashtable<uintptr_t, bool>* _table;

 public:
  VerifyLoadedHeapEmbeddedPointers(ResourceHashtable<uintptr_t, bool>* table) : _table(table) {}

  virtual void do_oop(narrowOop* p) {
    // This should be called before the loaded region is modified, so all the embedded pointers
    // must be null, or must point to a valid object in the loaded region.
    narrowOop v = *p;
    if (!CompressedOops::is_null(v)) {
      oop o = CompressedOops::decode_not_null(v);
      uintptr_t u = cast_from_oop<uintptr_t>(o);
      ArchiveHeapLoader::assert_in_loaded_heap(u);
      guarantee(_table->contains(u), "must point to beginning of object in loaded archived region");
    }
  }
  virtual void do_oop(oop* p) {
    // Uncompressed oops are not supported by loaded heaps.
    Unimplemented();
  }
};

// TODO Call me!
void ArchiveHeapLoader::verify_loaded_heap() {
  log_info(cds, heap)("Verify all oops and pointers in loaded heap");

  ResourceMark rm;
  ResourceHashtable<uintptr_t, bool> table;
  VerifyLoadedHeapEmbeddedPointers verifier(&table);
  HeapWord* bottom = (HeapWord*)_loaded_heap_bottom;
  HeapWord* top    = (HeapWord*)_loaded_heap_top;

  for (HeapWord* p = bottom; p < top; ) {
    oop o = cast_to_oop(p);
    table.put(cast_from_oop<uintptr_t>(o), true);
    p += o->size();
  }

  for (HeapWord* p = bottom; p < top; ) {
    oop o = cast_to_oop(p);
    o->oop_iterate(&verifier);
    p += o->size();
  }
}

static size_t _new_load_heap_size; // total size of heap region, in number of HeapWords
static char* _new_load_heap_buff;

bool ArchiveHeapLoader::new_load_heap_region(FileMapInfo* mapinfo) {
  _new_load_heap_buff = FileMapInfo::current_info()->new_map_heap(_new_load_heap_size);
  // FIXME -- do crc check here

  ArchiveHeapLoader::fixup_region();

  return (_new_load_heap_buff != nullptr);
}

class NewQuickLoader {
public:
  static HeapWord* mem_allocate_raw(size_t size) {
    bool gc_overhead_limit_was_exceeded;
    HeapWord* hw = Universe::heap()->mem_allocate(size, &gc_overhead_limit_was_exceeded);
    assert(hw != nullptr, "must not fail");
    return hw;
  }
};

template <bool COOPS, bool RAW_ALLOC>
class NewQuickLoaderImpl : public StackObj {
  HeapWord* _stream_bottom;
  HeapWord* _stream_top;

  // oop relocation
  BitMapView _oopmap;

  // native pointer relocation
  BitMapView _ptrmap;
  BitMap::idx_t _next_native_ptr_idx;
  HeapWord* _next_native_ptr_in_stream;

  struct Block {
    HeapWord* _bottom;
    HeapWord* _top;
    Block(HeapWord* b, HeapWord* t) : _bottom(b), _top(t) {}
    Block() : _bottom(nullptr), _top(nullptr) {}
  };

  GrowableArray<Block> _allocated_blocks;

  HeapWord* _last_block_bottom;
  HeapWord* _last_oop_top;
  DEBUG_ONLY(oop _lowest_materialized_oop;)
  DEBUG_ONLY(oop _highest_materialized_oop;)

public:
  inline NewQuickLoaderImpl() {
    assert(COOPS == UseCompressedOops, "sanity");
    _stream_bottom = (HeapWord*)_new_load_heap_buff;
    _stream_top    = _stream_bottom + _new_load_heap_size;

    _last_block_bottom = nullptr;
    _last_oop_top = nullptr;
    init_oopmap();
    init_ptrmap();

    DEBUG_ONLY(_lowest_materialized_oop = nullptr);
    DEBUG_ONLY(_highest_materialized_oop = nullptr);
  }

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
  inline oop load_archive_heap(TRAPS) {
    copy_objects(_stream_bottom, _stream_top, CHECK_NULL);
    relocate_oop_pointers();
    size_t heap_roots_word_offset = FileMapInfo::current_info()->heap_roots_offset() / HeapWordSize;
    return *(oop*)(_stream_bottom + heap_roots_word_offset);
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

  NOINLINE void add_new_block(HeapWord* new_obj) {
    if (_last_block_bottom) {
      _allocated_blocks.append(Block(_last_block_bottom, _last_oop_top));
    }
    _last_block_bottom = new_obj;
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

  void init_oopmap() {
    FileMapInfo::current_info()->map_bitmap_region();
    FileMapRegion* heap_region = FileMapInfo::current_info()->region_at(MetaspaceShared::hp);
    FileMapRegion* bitmap_region = FileMapInfo::current_info()->region_at(MetaspaceShared::bm);

    address start = (address)(bitmap_region->mapped_base()) + heap_region->oopmap_offset();
    _oopmap = BitMapView((BitMap::bm_word_t*)start, heap_region->oopmap_size_in_bits());
  }

  template <typename T>
  class OopPatcherBase : public BitMapClosure {
  protected:
    NewQuickLoaderImpl<COOPS, RAW_ALLOC>* _loader;
    T* _base;
    address _stream_bottom;
  public:
    OopPatcherBase(NewQuickLoaderImpl<COOPS, RAW_ALLOC>* loader, T* base) : _loader(loader), _base(base) {
      _stream_bottom = (address)_loader->_stream_bottom;
    }

    inline void patch(T* p, size_t pointee_byte_offset) {
      address pointee_stream_header_addr = _stream_bottom + pointee_byte_offset;
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
    NarrowOopPatcher(NewQuickLoaderImpl<COOPS, RAW_ALLOC>* loader, narrowOop* base) : OopPatcherBase<narrowOop>(loader, base) {
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
    OopPatcher(NewQuickLoaderImpl<COOPS, RAW_ALLOC>* loader, oop* base) : OopPatcherBase<oop>(loader, base) {
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
    size_t first_word_for_reloc = FileMapInfo::current_info()->heap_first_quick_reloc() / HeapWordSize;
    BitMap::idx_t lowest_bit = (BitMap::idx_t)(first_word_for_reloc * scale);

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
          assert(dumptime_oop_shift == 0 || dumptime_oop_shift == 3,
                 "other values are not supproted by the C++ templates");
          if (dumptime_oop_shift == 0) {
            NarrowOopPatcher<0> patcher(this, (narrowOop*)base);
            _oopmap.iterate(&patcher, start_bit, end_bit);
          } else {
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

  void update_next_native_ptr_in_stream(BitMap::idx_t increment) {
    _next_native_ptr_idx += increment;
    _next_native_ptr_idx = _ptrmap.find_first_set_bit(_next_native_ptr_idx);
    if (_next_native_ptr_idx < _ptrmap.size()) {
      _next_native_ptr_in_stream = _stream_bottom + _next_native_ptr_idx;
    } else {
      // we have relocated all native pointers
      _next_native_ptr_in_stream = _stream_top;
    }
  }

  void relocate_one_native_pointer(HeapWord* stream, HeapWord* m) {
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

  inline oop allocate(HeapWord* stream, size_t& size, TRAPS) {
    oop o = cast_to_oop(stream); // "original" from the stream
    size = o->size();

    if (RAW_ALLOC) {
      return cast_to_oop(NewQuickLoader::mem_allocate_raw(size));
    }
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
};

#define LOAD_ARCHIVE_HEAP_WITH_TEMPLATE(a, b) \
   NewQuickLoaderImpl<a, b> loader; \
   roots = loader.load_archive_heap(THREAD);

void ArchiveHeapLoader::new_fixup_region(TRAPS) {
  if (_new_load_heap_buff == nullptr) {
    FileMapInfo::current_info()->unmap_region(MetaspaceShared::bm);
    return;
  }

  log_info(cds)("new heap loading: start");

  ResourceMark rm;
  jlong time_started = os::thread_cpu_time(THREAD);
  jlong time_done;
  oop roots;

  // The parameters are <UseCompressedOops, NahlRawAlloc>
  if (UseCompressedOops) {
    if (NahlRawAlloc) {
      LOAD_ARCHIVE_HEAP_WITH_TEMPLATE(true, true);
    } else {
      LOAD_ARCHIVE_HEAP_WITH_TEMPLATE(true, false);
    }
  } else {
    if (NahlRawAlloc) {
      LOAD_ARCHIVE_HEAP_WITH_TEMPLATE(false, true);
    } else {
      LOAD_ARCHIVE_HEAP_WITH_TEMPLATE(false, false);
    }
  }
  _is_loaded = true;
  HeapShared::init_roots(roots);
  log_info(cds)("new heap loading: roots = " INTPTR_FORMAT, p2i(roots));
  time_done = os::thread_cpu_time(THREAD);
  log_info(cds, gc)("Load Time: " JLONG_FORMAT, (time_done - time_started));

  FileMapInfo::current_info()->unmap_region(MetaspaceShared::bm);
  return;
}

#endif // INCLUDE_CDS_JAVA_HEAP
