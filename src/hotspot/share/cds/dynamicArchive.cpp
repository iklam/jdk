/*
 * Copyright (c) 2019, 2023, Oracle and/or its affiliates. All rights reserved.
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
#include "cds/archiveBuilder.hpp"
#include "cds/archiveUtils.inline.hpp"
#include "cds/cds_globals.hpp"
#include "cds/classPrelinker.hpp"
#include "cds/dynamicArchive.hpp"
#include "cds/lambdaFormInvokers.hpp"
#include "cds/metaspaceShared.hpp"
#include "classfile/classLoaderData.inline.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "classfile/vmSymbols.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gcVMOperations.hpp"
#include "gc/shared/gc_globals.hpp"
#include "jvm.h"
#include "logging/log.hpp"
#include "memory/metaspaceClosure.hpp"
#include "memory/resourceArea.hpp"
#include "oops/klass.inline.hpp"
#include "runtime/arguments.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/vmThread.hpp"
#include "runtime/vmOperations.hpp"
#include "utilities/align.hpp"
#include "utilities/bitMap.inline.hpp"


class DynamicArchiveBuilder : public ArchiveBuilder {
  const char* _archive_name;
public:
  DynamicArchiveBuilder(const char* archive_name) : _archive_name(archive_name) {}
  void mark_pointer(address* ptr_loc) {
    ArchivePtrMarker::mark_pointer(ptr_loc);
  }

  static int dynamic_dump_method_comparator(Method* a, Method* b) {
    Symbol* a_name = a->name();
    Symbol* b_name = b->name();

    if (a_name == b_name) {
      return 0;
    }

    u4 a_offset = ArchiveBuilder::current()->any_to_offset_u4(a_name);
    u4 b_offset = ArchiveBuilder::current()->any_to_offset_u4(b_name);

    if (a_offset < b_offset) {
      return -1;
    } else {
      assert(a_offset > b_offset, "must be");
      return 1;
    }
  }

public:
  DynamicArchiveHeader *_header;

  void init_header();
  void release_header();
  void post_dump();
  void sort_methods();
  void sort_methods(InstanceKlass* ik) const;
  void remark_pointers_for_instance_klass(InstanceKlass* k, bool should_mark) const;
  void write_archive(char* serialized_data);

public:
  DynamicArchiveBuilder() : ArchiveBuilder() { }

  // Do this before and after the archive dump to see if any corruption
  // is caused by dynamic dumping.
  void verify_universe(const char* info) {
    if (VerifyBeforeExit) {
      log_info(cds)("Verify %s", info);
      // Among other things, this ensures that Eden top is correct.
      Universe::heap()->prepare_for_verify();
      Universe::verify(info);
    }
  }

  void doit() {
    verify_universe("Before CDS dynamic dump");
    DEBUG_ONLY(SystemDictionaryShared::NoClassLoadingMark nclm);

    // Block concurrent class unloading from changing the _dumptime_table
    MutexLocker ml(DumpTimeTable_lock, Mutex::_no_safepoint_check_flag);
    SystemDictionaryShared::check_excluded_classes();

    if (SystemDictionaryShared::is_dumptime_table_empty()) {
      log_warning(cds, dynamic)("There is no class to be included in the dynamic archive.");
      return;
    }

    // save dumptime tables
    SystemDictionaryShared::clone_dumptime_tables();

    init_header();
    gather_source_objs();
    reserve_buffer();

    log_info(cds, dynamic)("Copying %d klasses and %d symbols",
                           klasses()->length(), symbols()->length());
    dump_rw_metadata();
    dump_ro_metadata();
    relocate_metaspaceobj_embedded_pointers();
    relocate_roots();

    verify_estimate_size(_estimated_metaspaceobj_bytes, "MetaspaceObjs");

    char* serialized_data;
    {
      // Write the symbol table and system dictionaries to the RO space.
      // Note that these tables still point to the *original* objects, so
      // they would need to call DynamicArchive::original_to_target() to
      // get the correct addresses.
      assert(current_dump_space() == ro_region(), "Must be RO space");
      SymbolTable::write_to_archive(symbols());

      ArchiveBuilder::OtherROAllocMark mark;
      SystemDictionaryShared::write_to_archive(false);
      DynamicArchive::dump_additional_data();

      serialized_data = ro_region()->top();
      WriteClosure wc(ro_region());
      SymbolTable::serialize_shared_table_header(&wc, false);
      SystemDictionaryShared::serialize_dictionary_headers(&wc, false);
      DynamicArchive::serialize_additional_data(&wc);
    }

    verify_estimate_size(_estimated_hashtable_bytes, "Hashtables");

    sort_methods();

    log_info(cds)("Make classes shareable");
    make_klasses_shareable();

    log_info(cds)("Adjust lambda proxy class dictionary");
    SystemDictionaryShared::adjust_lambda_proxy_class_dictionary();

    relocate_to_requested();

    write_archive(serialized_data);
    release_header();

    post_dump();

    // Restore dumptime tables
    SystemDictionaryShared::restore_dumptime_tables();

    assert(_num_dump_regions_used == _total_dump_regions, "must be");
    verify_universe("After CDS dynamic dump");
  }

  virtual void iterate_roots(MetaspaceClosure* it, bool is_relocating_pointers) {
    FileMapInfo::metaspace_pointers_do(it);
    SystemDictionaryShared::dumptime_classes_do(it);
  }
};

void DynamicArchiveBuilder::init_header() {
  FileMapInfo* mapinfo = new FileMapInfo(_archive_name, false);
  assert(FileMapInfo::dynamic_info() == mapinfo, "must be");
  FileMapInfo* base_info = FileMapInfo::current_info();
  // header only be available after populate_header
  mapinfo->populate_header(base_info->core_region_alignment());
  _header = mapinfo->dynamic_header();

  _header->set_base_header_crc(base_info->crc());
  for (int i = 0; i < MetaspaceShared::n_regions; i++) {
    _header->set_base_region_crc(i, base_info->region_crc(i));
  }
}

void DynamicArchiveBuilder::release_header() {
  // We temporarily allocated a dynamic FileMapInfo for dumping, which makes it appear we
  // have mapped a dynamic archive, but we actually have not. We are in a safepoint now.
  // Let's free it so that if class loading happens after we leave the safepoint, nothing
  // bad will happen.
  assert(SafepointSynchronize::is_at_safepoint(), "must be");
  FileMapInfo *mapinfo = FileMapInfo::dynamic_info();
  assert(mapinfo != nullptr && _header == mapinfo->dynamic_header(), "must be");
  delete mapinfo;
  assert(!DynamicArchive::is_mapped(), "must be");
  _header = nullptr;
}

void DynamicArchiveBuilder::post_dump() {
  ArchivePtrMarker::reset_map_and_vs();
  ClassPrelinker::dispose();
}

void DynamicArchiveBuilder::sort_methods() {
  InstanceKlass::disable_method_binary_search();
  for (int i = 0; i < klasses()->length(); i++) {
    Klass* k = klasses()->at(i);
    if (k->is_instance_klass()) {
      sort_methods(InstanceKlass::cast(k));
    }
  }
}

// The address order of the copied Symbols may be different than when the original
// klasses were created. Re-sort all the tables. See Method::sort_methods().
void DynamicArchiveBuilder::sort_methods(InstanceKlass* ik) const {
  assert(ik != nullptr, "DynamicArchiveBuilder currently doesn't support dumping the base archive");
  if (MetaspaceShared::is_in_shared_metaspace(ik)) {
    // We have reached a supertype that's already in the base archive
    return;
  }

  if (ik->java_mirror() == nullptr) {
    // null mirror means this class has already been visited and methods are already sorted
    return;
  }
  ik->remove_java_mirror();

  if (log_is_enabled(Debug, cds, dynamic)) {
    ResourceMark rm;
    log_debug(cds, dynamic)("sorting methods for " PTR_FORMAT " (" PTR_FORMAT ") %s",
                            p2i(ik), p2i(to_requested(ik)), ik->external_name());
  }

  // Method sorting may re-layout the [iv]tables, which would change the offset(s)
  // of the locations in an InstanceKlass that would contain pointers. Let's clear
  // all the existing pointer marking bits, and re-mark the pointers after sorting.
  remark_pointers_for_instance_klass(ik, false);

  // Make sure all supertypes have been sorted
  sort_methods(ik->java_super());
  Array<InstanceKlass*>* interfaces = ik->local_interfaces();
  int len = interfaces->length();
  for (int i = 0; i < len; i++) {
    sort_methods(interfaces->at(i));
  }

#ifdef ASSERT
  if (ik->methods() != nullptr) {
    for (int m = 0; m < ik->methods()->length(); m++) {
      Symbol* name = ik->methods()->at(m)->name();
      assert(MetaspaceShared::is_in_shared_metaspace(name) || is_in_buffer_space(name), "must be");
    }
  }
  if (ik->default_methods() != nullptr) {
    for (int m = 0; m < ik->default_methods()->length(); m++) {
      Symbol* name = ik->default_methods()->at(m)->name();
      assert(MetaspaceShared::is_in_shared_metaspace(name) || is_in_buffer_space(name), "must be");
    }
  }
#endif

  Method::sort_methods(ik->methods(), /*set_idnums=*/true, dynamic_dump_method_comparator);
  if (ik->default_methods() != nullptr) {
    Method::sort_methods(ik->default_methods(), /*set_idnums=*/false, dynamic_dump_method_comparator);
  }
  if (ik->is_linked()) {
    // If the class has already been linked, we must relayout the i/v tables, whose order depends
    // on the method sorting order.
    // If the class is unlinked, we cannot layout the i/v tables yet. This is OK, as the
    // i/v tables will be initialized at runtime after bytecode verification.
    ik->vtable().initialize_vtable();
    ik->itable().initialize_itable();
  }

  // Set all the pointer marking bits after sorting.
  remark_pointers_for_instance_klass(ik, true);
}

template<bool should_mark>
class PointerRemarker: public MetaspaceClosure {
public:
  virtual bool do_ref(Ref* ref, bool read_only) {
    if (should_mark) {
      ArchivePtrMarker::mark_pointer(ref->addr());
    } else {
      ArchivePtrMarker::clear_pointer(ref->addr());
    }
    return false; // don't recurse
  }
};

void DynamicArchiveBuilder::remark_pointers_for_instance_klass(InstanceKlass* k, bool should_mark) const {
  if (should_mark) {
    PointerRemarker<true> marker;
    k->metaspace_pointers_do(&marker);
    marker.finish();
  } else {
    PointerRemarker<false> marker;
    k->metaspace_pointers_do(&marker);
    marker.finish();
  }
}

void DynamicArchiveBuilder::write_archive(char* serialized_data) {
  Array<u8>* table = FileMapInfo::saved_shared_path_table().table();
  SharedPathTable runtime_table(table, FileMapInfo::shared_path_table().size());
  _header->set_shared_path_table(runtime_table);
  _header->set_serialized_data(serialized_data);

  FileMapInfo* dynamic_info = FileMapInfo::dynamic_info();
  assert(dynamic_info != nullptr, "Sanity");

  dynamic_info->open_for_write();
  ArchiveBuilder::write_archive(dynamic_info, nullptr, nullptr, nullptr, nullptr);

  address base = _requested_dynamic_archive_bottom;
  address top  = _requested_dynamic_archive_top;
  size_t file_size = pointer_delta(top, base, sizeof(char));

  log_info(cds, dynamic)("Written dynamic archive " PTR_FORMAT " - " PTR_FORMAT
                         " [" UINT32_FORMAT " bytes header, " SIZE_FORMAT " bytes total]",
                         p2i(base), p2i(top), _header->header_size(), file_size);

  log_info(cds, dynamic)("%d klasses; %d symbols", klasses()->length(), symbols()->length());
}

class VM_PopulateDynamicDumpSharedSpace: public VM_GC_Sync_Operation {
  DynamicArchiveBuilder _builder;
public:
  VM_PopulateDynamicDumpSharedSpace(const char* archive_name)
  : VM_GC_Sync_Operation(), _builder(archive_name) {}
  VMOp_Type type() const { return VMOp_PopulateDumpSharedSpace; }
  void doit() {
    ResourceMark rm;
    if (AllowArchivingWithJavaAgent) {
      log_warning(cds)("This archive was created with AllowArchivingWithJavaAgent. It should be used "
              "for testing purposes only and should not be used in a production environment");
    }
    FileMapInfo::check_nonempty_dir_in_shared_path_table();

    _builder.doit();
  }
  ~VM_PopulateDynamicDumpSharedSpace() {
    LambdaFormInvokers::cleanup_regenerated_classes();
  }
};

void DynamicArchive::check_for_dynamic_dump() {
  if (DynamicDumpSharedSpaces && !UseSharedSpaces) {
    // This could happen if SharedArchiveFile has failed to load:
    // - -Xshare:off was specified
    // - SharedArchiveFile points to an non-existent file.
    // - SharedArchiveFile points to an archive that has failed CRC check
    // - SharedArchiveFile is not specified and the VM doesn't have a compatible default archive

#define __THEMSG " is unsupported when base CDS archive is not loaded. Run with -Xlog:cds for more info."
    if (RecordDynamicDumpInfo) {
      vm_exit_during_initialization("-XX:+RecordDynamicDumpInfo" __THEMSG, nullptr);
    } else {
      assert(ArchiveClassesAtExit != nullptr, "sanity");
      log_warning(cds)("-XX:ArchiveClassesAtExit" __THEMSG);
    }
#undef __THEMSG
    DynamicDumpSharedSpaces = false;
  }
}

void DynamicArchive::dump_at_exit(JavaThread* current, const char* archive_name) {
  ExceptionMark em(current);
  ResourceMark rm(current);
  HandleMark hm(current);

  if (!DynamicDumpSharedSpaces || archive_name == nullptr) {
    return;
  }

  log_info(cds, dynamic)("Preparing for dynamic dump at exit in thread %s", current->name());
  DynamicArchive::init_training_data(); // temp -- example

  JavaThread* THREAD = current; // For TRAPS processing related to link_shared_classes
  MetaspaceShared::link_shared_classes(false/*not from jcmd*/, THREAD);
  if (!HAS_PENDING_EXCEPTION) {
    // copy shared path table to saved.
    FileMapInfo::clone_shared_path_table(current);
    if (!HAS_PENDING_EXCEPTION) {
      VM_PopulateDynamicDumpSharedSpace op(archive_name);
      VMThread::execute(&op);
      return;
    }
  }

  // One of the prepatory steps failed
  oop ex = current->pending_exception();
  log_error(cds)("Dynamic dump has failed");
  log_error(cds)("%s: %s", ex->klass()->external_name(),
                 java_lang_String::as_utf8_string(java_lang_Throwable::message(ex)));
  CLEAR_PENDING_EXCEPTION;
  DynamicDumpSharedSpaces = false;  // Just for good measure
}

// This is called by "jcmd VM.cds dynamic_dump"
void DynamicArchive::dump_for_jcmd(const char* archive_name, TRAPS) {
  assert(UseSharedSpaces && RecordDynamicDumpInfo, "already checked in arguments.cpp");
  assert(ArchiveClassesAtExit == nullptr, "already checked in arguments.cpp");
  assert(DynamicDumpSharedSpaces, "already checked by check_for_dynamic_dump() during VM startup");
  MetaspaceShared::link_shared_classes(true/*from jcmd*/, CHECK);
  // copy shared path table to saved.
  FileMapInfo::clone_shared_path_table(CHECK);
  VM_PopulateDynamicDumpSharedSpace op(archive_name);
  VMThread::execute(&op);
}

bool DynamicArchive::validate(FileMapInfo* dynamic_info) {
  assert(!dynamic_info->is_static(), "must be");
  // Check if the recorded base archive matches with the current one
  FileMapInfo* base_info = FileMapInfo::current_info();
  DynamicArchiveHeader* dynamic_header = dynamic_info->dynamic_header();

  // Check the header crc
  if (dynamic_header->base_header_crc() != base_info->crc()) {
    log_warning(cds)("Dynamic archive cannot be used: static archive header checksum verification failed.");
    return false;
  }

  // Check each space's crc
  for (int i = 0; i < MetaspaceShared::n_regions; i++) {
    if (dynamic_header->base_region_crc(i) != base_info->region_crc(i)) {
      log_warning(cds)("Dynamic archive cannot be used: static archive region #%d checksum verification failed.", i);
      return false;
    }
  }

  return true;
}

/*

This is an example of writing additional data into the CDS archive that can be readily accessed at runtime.

For the leyden-premain CDS exercises, at this point (2023/04/06), it may be easier to use the CDS dynamic archive:

[1] This makes the "training run" and stores all the classes used by the HelloWorld app into HelloWorld.jsa.
    It also stores the "training data" that are gathered during the training run.

    $ java -Xlog:cds -cp HelloWorld.jar -XX:ArchiveClassesAtExit=HelloWorld.jsa HelloWorld


[2] This loads the HelloWorld classes from HelloWorld.jsa, and some system classes from the base archive:

    $ java -Xlog:cds -cp HelloWorld.jar -XX:SharedArchiveFile=HelloWorld.jsa HelloWorld
    ....
    # the range of the metadata (InstanceKlass, Method, etc)
    [0.014s][info][cds] Mapped static  region #0 at base 0x0000000800000000 top 0x0000000800505000 (ReadWrite)
    [0.014s][info][cds] Mapped static  region #1 at base 0x0000000800505000 top 0x0000000800d20000 (ReadOnly)
    [0.014s][info][cds] Mapped dynamic region #0 at base 0x0000000800d20000 top 0x0000000800d2f000 (ReadWrite)
    [0.014s][info][cds] Mapped dynamic region #1 at base 0x0000000800d2f000 top 0x0000000800d3f000 (ReadOnly)
    ....
    # the fake "training data" (the array is in "dynamic region #1")
    _archived_training_data = 0x0000000800d3e5a0
    _archived_training_data[ 0] = 0x0000000800505308 (java/lang/Boolean)
    _archived_training_data[ 1] = 0x0000000800505320 (java/lang/Character)
    _archived_training_data[ 2] = 0x0000000800505340 (java/lang/Character$CharacterCache)
    _archived_training_data[ 3] = 0x0000000800505368 (java/lang/CharacterDataLatin1)
    _archived_training_data[ 4] = 0x0000000800505390 (java/lang/Float)
    _archived_training_data[ 5] = 0x00000008005053a8 (java/lang/Double)
    _archived_training_data[ 6] = 0x00000008005053c0 (java/lang/Byte)
    _archived_training_data[ 7] = 0x00000008005053d8 (java/lang/Byte$ByteCache)
    _archived_training_data[ 8] = 0x00000008005053f8 (java/lang/Short)
    _archived_training_data[ 9] = 0x0000000800505410 (java/lang/Short$ShortCache)
    _archived_training_data[10] = 0x0000000800505430 (java/lang/Integer)
    _archived_training_data[11] = 0x0000000800505448 (java/lang/Integer$IntegerCache)
    _archived_training_data[12] = 0x0000000800505470 (java/lang/Long)
    _archived_training_data[13] = 0x0000000800505488 (java/lang/Long$LongCache)
    _aot_code = 0x0000000800d3e618
    _aot_code size = 12345 bytes

*/

static GrowableArrayCHeap<Symbol*, mtClassShared>* _live_training_data = nullptr;
static Array<Symbol*>* _archived_training_data = nullptr;
static Array<char>* _aot_code = nullptr;

// This is called before we enter the VM_PopulateDynamicDumpSharedSpace safepoint.
// Theoretically, this could be called by various CompilerThreads to store some
// training data data into _live_training_data (with proper synchronization),
// and would be part of the Compiler class instead.
void DynamicArchive::init_training_data() {
  _live_training_data = new GrowableArrayCHeap<Symbol*, mtClassShared>(150);

  _live_training_data->append(vmSymbols::java_lang_Boolean());
  _live_training_data->append(vmSymbols::java_lang_Character());
  _live_training_data->append(vmSymbols::java_lang_Character_CharacterCache());
  _live_training_data->append(vmSymbols::java_lang_CharacterDataLatin1());
  _live_training_data->append(vmSymbols::java_lang_Float());
  _live_training_data->append(vmSymbols::java_lang_Double());
  _live_training_data->append(vmSymbols::java_lang_Byte());
  _live_training_data->append(vmSymbols::java_lang_Byte_ByteCache());
  _live_training_data->append(vmSymbols::java_lang_Short());
  _live_training_data->append(vmSymbols::java_lang_Short_ShortCache());
  _live_training_data->append(vmSymbols::java_lang_Integer());
  _live_training_data->append(vmSymbols::java_lang_Integer_IntegerCache());
  _live_training_data->append(vmSymbols::java_lang_Long());
  _live_training_data->append(vmSymbols::java_lang_Long_LongCache());                              
}

// This is called inside the VM_PopulateDynamicDumpSharedSpace safepoint
void DynamicArchive::dump_additional_data() {
  // The following could be refactored to a call to Compiler::dump_training_data(), etc.
  int len = _live_training_data->length();
  _archived_training_data = ArchiveBuilder::new_ro_array<Symbol*>(len);

 // FIXME: we should have a utility function that does the copying and ptr marking for us.
  for (int i = 0; i < len; i++) {
    Symbol* s = _live_training_data->at(i);
    _archived_training_data->at_put(i, s);
    ArchivePtrMarker::mark_pointer(_archived_training_data->adr_at(i)); // must mark the pointer
  }

  // Allocate some space in the archive to be used to store AOT code.
  _aot_code =  ArchiveBuilder::new_ro_array<char>(12345);
}

void DynamicArchive::serialize_additional_data(SerializeClosure* soc) {
  // The following could be refactored to a call to Compiler::serialize_training_data(), etc.
  soc->do_ptr((void**)&_archived_training_data);
  soc->do_ptr((void**)&_aot_code);

  if (soc->reading()) {
    tty->print_cr("_archived_training_data = " INTPTR_FORMAT, p2i(_archived_training_data));
    if (_archived_training_data != nullptr) {
      for (int i = 0; i < _archived_training_data->length(); i++) {
        ResourceMark rm;
        Symbol* s = _archived_training_data->at(i);
        tty->print_cr("_archived_training_data[%2d] = " INTPTR_FORMAT " (%s)", i, p2i(s), s->as_quoted_ascii());
      }
    }

    tty->print_cr("_aot_code = " INTPTR_FORMAT, p2i(_aot_code));
    if (_aot_code != nullptr) {
      tty->print_cr("_aot_code size = %d bytes", _aot_code->length());
    }
  }
}
