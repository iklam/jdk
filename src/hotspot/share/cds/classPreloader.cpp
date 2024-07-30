/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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
#include "cds/cdsAccess.hpp"
#include "cds/cdsConfig.hpp"
#include "cds/cdsProtectionDomain.hpp"
#include "cds/classPrelinker.hpp"
#include "cds/classPreloader.hpp"
#include "cds/heapShared.hpp"
#include "cds/lambdaFormInvokers.inline.hpp"
#include "classfile/classLoader.hpp"
#include "classfile/classLoaderExt.hpp"
#include "classfile/dictionary.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "classfile/vmClasses.hpp"
#include "compiler/compilationPolicy.hpp"
#include "memory/resourceArea.hpp"
#include "oops/constantPool.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/trainingData.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/perfData.inline.hpp"
#include "runtime/timer.hpp"
#include "services/management.hpp"

// List of classes to be loaded at VM bootstrap.
class AOTLoadedClassTable {
  static AOTLoadedClassTable _for_static_archive;
  static AOTLoadedClassTable _for_dynamic_archive;

  bool _is_static_archive; // Used for debugging (FIXME TODO....)

  Array<InstanceKlass*>* _boot;  // only java.base classes
  Array<InstanceKlass*>* _boot2; // boot classes in other modules
  Array<InstanceKlass*>* _platform;
  Array<InstanceKlass*>* _app;

public:
  AOTLoadedClassTable(bool is_static_archive) :
    _is_static_archive(is_static_archive),
    _boot(nullptr), _boot2(nullptr),
    _platform(nullptr), _app(nullptr) {}

  static AOTLoadedClassTable* for_static_archive()  { return &_for_static_archive; }
  static AOTLoadedClassTable* for_dynamic_archive() { return &_for_dynamic_archive; }

  static AOTLoadedClassTable* get(bool is_static_archive) {
    return is_static_archive ? for_static_archive() : for_dynamic_archive();
  }

  Array<InstanceKlass*>* boot()     const { return _boot;     }
  Array<InstanceKlass*>* boot2()    const { return _boot2;    }
  Array<InstanceKlass*>* platform() const { return _platform; }
  Array<InstanceKlass*>* app()      const { return _app;      }

  void set_boot    (Array<InstanceKlass*>* value) { _boot     = value; }
  void set_boot2   (Array<InstanceKlass*>* value) { _boot2    = value; }
  void set_platform(Array<InstanceKlass*>* value) { _platform = value; }
  void set_app     (Array<InstanceKlass*>* value) { _app      = value; }

  void serialize(SerializeClosure* soc);
};

AOTLoadedClassTable AOTLoadedClassTable::_for_static_archive(true);
AOTLoadedClassTable AOTLoadedClassTable::_for_dynamic_archive(false);

AOTLoadedClassRecorder::ClassesTable* AOTLoadedClassRecorder::_vm_classes = nullptr;
AOTLoadedClassRecorder::ClassesTable* AOTLoadedClassRecorder::_candidates = nullptr;
GrowableArrayCHeap<InstanceKlass*, mtClassShared>* AOTLoadedClassRecorder::_sorted_candidates = nullptr;

Array<InstanceKlass*>* AOTLoadedClassManager::_unregistered_classes_from_preimage = nullptr;
bool AOTLoadedClassManager::_preloading_non_javavase_classes = false;

static PerfCounter* _perf_classes_preloaded = nullptr;
static PerfTickCounters* _perf_class_preload_counters = nullptr;

void AOTLoadedClassTable::serialize(SerializeClosure* soc) {
  soc->do_ptr((void**)&_boot);
  soc->do_ptr((void**)&_boot2);
  soc->do_ptr((void**)&_platform);
  soc->do_ptr((void**)&_app);

  if (_boot != nullptr && _boot->length() > 0) {
    CDSConfig::set_has_preloaded_classes();
  }
}

bool AOTLoadedClassRecorder::is_initialized() {
  assert(CDSConfig::is_dumping_archive(), "AOTLoadedClassRecorder is for CDS dumping only");
  return _vm_classes != nullptr;
}

void AOTLoadedClassRecorder::initialize() {
  assert(!is_initialized(), "sanity");

  _vm_classes = new (mtClass)ClassesTable();
  _candidates = new (mtClass)ClassesTable();
  _sorted_candidates = new GrowableArrayCHeap<InstanceKlass*, mtClassShared>(1000);

  for (auto id : EnumRange<vmClassID>{}) {
    add_vm_class(vmClasses::klass_at(id));
  }

  assert(is_initialized(), "sanity");
}

void AOTLoadedClassRecorder::dispose() {
  assert(is_initialized(), "sanity");

  delete _vm_classes;
  delete _candidates;
  delete _sorted_candidates;
  _vm_classes = nullptr;
  _candidates = nullptr;
  _sorted_candidates = nullptr;

  assert(!is_initialized(), "sanity");
}

bool AOTLoadedClassRecorder::is_vm_class(InstanceKlass* ik) {
  assert(is_initialized(), "sanity");
  return (_vm_classes->get(ik) != nullptr);
}

void AOTLoadedClassRecorder::add_vm_class(InstanceKlass* ik) {
  assert(is_initialized(), "sanity");
  bool created;
  _vm_classes->put_if_absent(ik, &created);
  if (created) {
    add_candidate(ik);
    InstanceKlass* super = ik->java_super();
    if (super != nullptr) {
      add_vm_class(super);
    }
    Array<InstanceKlass*>* ifs = ik->local_interfaces();
    for (int i = 0; i < ifs->length(); i++) {
      add_vm_class(ifs->at(i));
    }
  }
}

bool AOTLoadedClassRecorder::is_candidate(InstanceKlass* ik) {
  return (_candidates->get(ik) != nullptr);
}

void AOTLoadedClassRecorder::add_candidate(InstanceKlass* ik) {
  _candidates->put_when_absent(ik, true);
  _sorted_candidates->append(ik);
}

bool AOTLoadedClassRecorder::try_add_candidate(InstanceKlass* ik) {
  assert(is_initialized(), "sanity");

  if (!PreloadSharedClasses || !SystemDictionaryShared::is_builtin(ik)) {
    return false;
  }

  if (is_candidate(ik)) { // already checked.
    return true;
  }

  if (ik->is_hidden()) {
    assert(ik->shared_class_loader_type() != ClassLoader::OTHER, "must have been set");
    if (!CDSConfig::is_dumping_invokedynamic()) {
      return false;
    }
    if (!SystemDictionaryShared::should_hidden_class_be_archived(ik)) {
      return false;
    }
  } else {
    // FIXME -- remove this check -- AOT-loaded classes require archived FMG

    // Do not AOT-load any module classes that are not from the modules images,
    // since such classes may not be loadable at runtime
    int scp_index = ik->shared_classpath_index();
    assert(scp_index >= 0, "must be");
    SharedClassPathEntry* scp_entry = FileMapInfo::shared_path(scp_index);
    if (scp_entry->in_named_module() && !scp_entry->is_modules_image()) {
      return false;
    }
  }

  if (MetaspaceObj::is_shared(ik) && CDSConfig::is_dumping_dynamic_archive() && CDSConfig::has_preloaded_classes()) {
    // This class has been marked as a AOT-loaded for the base archive, so no need to mark it as a candidate
    // for the dynamic archive.
    return true;
  }

#if 0 
  // TODO ... this is probably not needed??
  if (CDSConfig::is_dumping_final_static_archive()) {
    // We will re-use the preloading lists from the preimage archive, so there's no need to
    // come here.
    // FIXME -- no need to use PreloadedKlassRecorder at all??
  }
#endif

  InstanceKlass* s = ik->java_super();
  if (s != nullptr && !try_add_candidate(s)) {
    return false;
  }

  Array<InstanceKlass*>* interfaces = ik->local_interfaces();
  int num_interfaces = interfaces->length();
  for (int index = 0; index < num_interfaces; index++) {
    InstanceKlass* intf = interfaces->at(index);
    if (!try_add_candidate(intf)) {
      return false;
    }
  }

  add_candidate(ik);

  if (log_is_enabled(Info, cds, preload)) {
    ResourceMark rm;
    log_info(cds, preload)("%s %s", ArchiveUtils::class_category(ik), ik->external_name());
  }

  return true;
}

void AOTLoadedClassRecorder::write_to_archive() {
  assert(is_initialized(), "sanity");

  if (!PreloadSharedClasses) {
    // nothing to do
    return;
  }

  GrowableArray<Klass*>* klasses = ArchiveBuilder::current()->klasses();
  for (GrowableArrayIterator<Klass*> it = klasses->begin(); it != klasses->end(); ++it) {
    Klass* k = *it;
    if (k->is_instance_klass()) {
      try_add_candidate(InstanceKlass::cast(k));
    }
  }

  AOTLoadedClassTable* table = AOTLoadedClassTable::get(CDSConfig::is_dumping_static_archive());
  table->set_boot(write_classes(nullptr, true));
  table->set_boot2(write_classes(nullptr, false));
  table->set_platform(write_classes(SystemDictionary::java_platform_loader(), false));
  table->set_app(write_classes(SystemDictionary::java_system_loader(), false));
}

Array<InstanceKlass*>* AOTLoadedClassRecorder::write_classes(oop class_loader, bool is_javabase) {
  ResourceMark rm;
  GrowableArray<InstanceKlass*> list;

  for (int i = 0; i < _sorted_candidates->length(); i++) {
    InstanceKlass* ik = _sorted_candidates->at(i);
    if (ik->name()->equals("HelloWorld")) {
      tty->print_cr("xxx");
    }
    if (ik->class_loader() != class_loader) {
      continue;
    }
    if ((ik->module() == ModuleEntryTable::javabase_moduleEntry()) != is_javabase) {
      continue;
    }

    list.append(ArchiveBuilder::current()->get_buffered_addr(ik));
  }

  if (list.length() == 0) {
    return nullptr;
  } else {
    const char* category = ArchiveUtils::class_category(ArchiveBuilder::current()->get_source_addr(list.at(0)));
    log_info(cds, preload)("written %d class(es) for category %s", list.length(), category);
    return ArchiveUtils::archive_array(&list);
  }
}

void AOTLoadedClassManager::record_unregistered_classes() {
  if (CDSConfig::is_dumping_preimage_static_archive()) {
    GrowableArray<InstanceKlass*> unreg_classes;
    GrowableArray<Klass*>* klasses = ArchiveBuilder::current()->klasses();
    for (int i = 0; i < klasses->length(); i++) {
      Klass* k = klasses->at(i);
      if (k->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(k);
        if (ik->is_shared_unregistered_class()) {
          unreg_classes.append((InstanceKlass*)ArchiveBuilder::get_buffered_klass(ik));
        }
      }
    }
    _unregistered_classes_from_preimage = ArchiveUtils::archive_array(&unreg_classes);
  } else {
    _unregistered_classes_from_preimage = nullptr;
  }
}

void AOTLoadedClassManager::serialize(SerializeClosure* soc, bool is_static_archive) {
  AOTLoadedClassTable::get(is_static_archive)->serialize(soc);

  if (is_static_archive) {
    soc->do_ptr((void**)&_unregistered_classes_from_preimage);
  }

  if (is_static_archive && soc->reading() && UsePerfData) {
    JavaThread* THREAD = JavaThread::current();
    NEWPERFEVENTCOUNTER(_perf_classes_preloaded, SUN_CLS, "preloadedClasses");
    NEWPERFTICKCOUNTERS(_perf_class_preload_counters, SUN_CLS, "classPreload");
  }
}

int AOTLoadedClassRecorder::num_platform_initiated_classes() {
  return 0;// FIXME
}

int AOTLoadedClassRecorder::num_app_initiated_classes() {
  return 0;// FIXME
}

volatile bool _class_preloading_finished = false;

bool AOTLoadedClassManager::class_preloading_finished() {
  if (!UseSharedSpaces) {
    return true;
  } else {
    // The ConstantPools of preloaded classes have references to other preloaded classes. We don't
    // want any Java code (including JVMCI compiler) to use these classes until all of them
    // are loaded.
    return Atomic::load_acquire(&_class_preloading_finished);
  }
}

bool AOTLoadedClassManager::is_preloading_non_javavase_classes() { // FIXME -- need a better name
  return !Universe::is_fully_initialized() && _preloading_non_javavase_classes;
}

// This function is called 4 times:
// preload only java.base classes
// preload boot classes outside of java.base
// preload classes for platform loader
// preload classes for app loader
void AOTLoadedClassManager::load(JavaThread* current, Handle loader) {
#ifdef ASSERT
  if (loader() == nullptr) {
    static bool first_time = true;
    if (first_time) {
      // FIXME -- assert that no java code has been executed up to this point.
      //
      // Reason: Here, only vmClasses have been loaded. However, their CP might
      // have some pre-resolved entries that point to classes that are loaded
      // only by this function! Any Java bytecode that uses such entries will
      // fail.
    }
    first_time = false;
  }
#endif // ASSERT

  if (UseSharedSpaces) {
    if (loader() != nullptr && !SystemDictionaryShared::has_platform_or_app_classes()) {
      // Non-boot classes might have been disabled due to command-line mismatch.
      Atomic::release_store(&_class_preloading_finished, true);
      return;
    }
    ResourceMark rm(current);
    ExceptionMark em(current);
    load_table(AOTLoadedClassTable::for_static_archive(),  loader, current); // cannot throw
    load_table(AOTLoadedClassTable::for_dynamic_archive(), loader, current); // cannot throw

    if (loader() != nullptr && loader() == SystemDictionary::java_system_loader()) {
      Atomic::release_store(&_class_preloading_finished, true);
    }
  }
  assert(!current->has_pending_exception(), "VM should have exited due to ExceptionMark");

  if (loader() != nullptr && loader() == SystemDictionary::java_system_loader()) {
    if (PrintTrainingInfo) {
      tty->print_cr("==================== archived_training_data ** after all classes preloaded ====================");
      TrainingData::print_archived_training_data_on(tty);
    }

    if (log_is_enabled(Info, cds, jit)) {
      CDSAccess::test_heap_access_api();
    }

    if (CDSConfig::is_dumping_final_static_archive()) {
      assert(_unregistered_classes_from_preimage != nullptr, "must be");
      for (int i = 0; i < _unregistered_classes_from_preimage->length(); i++) {
        InstanceKlass* ik = _unregistered_classes_from_preimage->at(i);
        SystemDictionaryShared::init_dumptime_info(ik);
        SystemDictionaryShared::add_unregistered_class(current, ik);
      }
    }
  }
}

void AOTLoadedClassManager::load_table(AOTLoadedClassTable* table, Handle loader, TRAPS) {
  PerfTraceTime timer(_perf_class_preload_counters);

  // ResourceMark is missing in the code below due to JDK-8307315
  ResourceMark rm(THREAD);
  if (loader() == nullptr) {
    load_classes(table->boot(), "boot ", loader, CHECK);

    _preloading_non_javavase_classes = true;
    load_classes(table->boot2(), "boot2", loader, CHECK);
    _preloading_non_javavase_classes = false;

  } else if (loader() == SystemDictionary::java_platform_loader()) {
    const char* category = "plat ";
    load_initiated_classes(THREAD, category, loader, table->boot());
    load_initiated_classes(THREAD, category, loader, table->boot2());

    //_preloading_non_javavase_classes = true;
    load_classes(table->platform(), category, loader, CHECK);
    //_preloading_non_javavase_classes = false;

    maybe_init_or_link(table->platform(), CHECK);
  } else {
    assert(loader() == SystemDictionary::java_system_loader(), "must be");
    const char* category = "app  ";
    load_initiated_classes(THREAD, category, loader, table->boot());
    load_initiated_classes(THREAD, category, loader, table->boot2());
    load_initiated_classes(THREAD, category, loader, table->platform());

    //_preloading_non_javavase_classes = true;
    load_classes(table->app(), category, loader, CHECK);
    //_preloading_non_javavase_classes = false;

    maybe_init_or_link(table->app(), CHECK);
  }

  if (loader() != nullptr) {
    HeapShared::initialize_default_subgraph_classes(loader, CHECK); // TODO: do we support subgraph classes for boot2??
  }

#if 0
  // Hmm, does JavacBench crash if this block is enabled??
  if (VerifyDuringStartup) {
    VM_Verify verify_op;
    VMThread::execute(&verify_op);
  }
#endif
}

void AOTLoadedClassManager::load_classes(Array<InstanceKlass*>* classes, const char* category, Handle loader, TRAPS) {
  if (classes == nullptr) {
    return;
  }

  ClassLoaderData* loader_data = ClassLoaderData::class_loader_data(loader());
  for (int i = 0; i < classes->length(); i++) {
    if (UsePerfData) {
      _perf_classes_preloaded->inc();
    }
    InstanceKlass* ik = classes->at(i);
    if (log_is_enabled(Info, cds, preload)) {
      ResourceMark rm;
      log_info(cds, preload)("%s %s%s", category, ik->external_name(),
                             ik->is_loaded() ? " (already loaded)" : "");
    }
    // FIXME Do not load proxy classes if FMG is disabled.

    if (!ik->is_loaded()) {
      if (ik->is_hidden()) {
        load_hidden_class(loader, ik, CHECK);
      } else {
        InstanceKlass* actual;
        if (loader() == nullptr) {
          if (!Universe::is_fully_initialized()) {
            load_class_quick(ik, loader_data, Handle(), CHECK);
            actual = ik;
          } else {
            actual = SystemDictionary::load_instance_class(ik->name(), loader, CHECK);
          }
        } else {
          // Note: we are not adding the locker objects into java.lang.ClassLoader::parallelLockMap, but
          // that should be harmless.
          actual = SystemDictionaryShared::find_or_load_shared_class(ik->name(), loader, CHECK);
        }

        if (actual != ik) {
          jvmti_agent_error(ik, actual, "preloaded");
        }
        assert(actual->is_loaded(), "must be");
      }
    }

    // FIXME assert - if FMG, package must be archived
  }
}

void AOTLoadedClassManager::load_initiated_classes(JavaThread* current, const char* category, Handle loader, Array<InstanceKlass*>* classes) {
  if (classes == nullptr) {
    return;
  }

  ClassLoaderData* loader_data = ClassLoaderData::class_loader_data(loader());
  MonitorLocker mu1(SystemDictionary_lock);
  for (int i = 0; i < classes->length(); i++) {
    InstanceKlass* ik = classes->at(i);
    assert(ik->is_loaded(), "must have already been loaded by a parent loader");
    if (ik->is_public()) {
      if (log_is_enabled(Info, cds, preload)) {
        ResourceMark rm;
        const char* defining_loader = (ik->class_loader() == nullptr ? "boot" : "plat");
        log_info(cds, preload)("%s %s (initiated, defined by %s)", category, ik->external_name(),
                               defining_loader);
      }
      SystemDictionary::preload_class(current, ik, loader_data);
    }
  }
}

// FIXME -- is this really correct? Do we need a special ClassLoaderData for each hidden class?
void AOTLoadedClassManager::load_hidden_class(Handle class_loader, InstanceKlass* ik, TRAPS) {
  DEBUG_ONLY({
      assert(ik->super() == vmClasses::Object_klass(), "must be");
      for (int i = 0; i < ik->local_interfaces()->length(); i++) {
        assert(ik->local_interfaces()->at(i)->is_loaded(), "must be");
      }
    });

  ClassLoaderData* loader_data = ClassLoaderData::class_loader_data(class_loader());
  if (class_loader() == nullptr) {
    ik->restore_unshareable_info(loader_data, Handle(), NULL, CHECK);
  } else {
    PackageEntry* pkg_entry = CDSProtectionDomain::get_package_entry_from_class(ik, class_loader);
    Handle protection_domain =
        CDSProtectionDomain::init_security_info(class_loader, ik, pkg_entry, CHECK);
    ik->restore_unshareable_info(loader_data, protection_domain, pkg_entry, CHECK);
  }
  SystemDictionary::load_shared_class_misc(ik, loader_data);
  ik->add_to_hierarchy(THREAD);
}

void AOTLoadedClassManager::load_class_quick(InstanceKlass* ik, ClassLoaderData* loader_data, Handle domain, TRAPS) {
  assert(!ik->is_loaded(), "sanity");

#ifdef ASSERT
  {
    InstanceKlass* super = ik->java_super();
    if (super != nullptr) {
      assert(super->is_loaded(), "must have been loaded");
    }
    Array<InstanceKlass*>* intfs = ik->local_interfaces();
    for (int i = 0; i < intfs->length(); i++) {
      assert(intfs->at(i)->is_loaded(), "must have been loaded");
    }
  }
#endif

  ik->restore_unshareable_info(loader_data, domain, nullptr, CHECK); // TODO: should we use ik->package()?
  SystemDictionary::load_shared_class_misc(ik, loader_data);

  // We are adding to the dictionary but can get away without
  // holding SystemDictionary_lock, as no other threads will be loading
  // classes at the same time.
  assert(!Universe::is_fully_initialized(), "sanity");
  Dictionary* dictionary = loader_data->dictionary();
  dictionary->add_klass(THREAD, ik->name(), ik);
  ik->add_to_hierarchy(THREAD);
  assert(ik->is_loaded(), "Must be in at least loaded state");
}

void AOTLoadedClassManager::jvmti_agent_error(InstanceKlass* expected, InstanceKlass* actual, const char* type) {
  if (actual->is_shared() && expected->name() == actual->name() &&
      LambdaFormInvokers::may_be_regenerated_class(expected->name())) {
    // For the 4 regenerated classes (such as java.lang.invoke.Invokers$Holder) there's one
    // in static archive and one in dynamic archive. If the dynamic archive is loaded, we
    // load the one from the dynamic archive.
    return;
  }
  ResourceMark rm;
  log_error(cds)("Unable to resolve %s class from CDS archive: %s", type, expected->external_name());
  log_error(cds)("Expected: " INTPTR_FORMAT ", actual: " INTPTR_FORMAT, p2i(expected), p2i(actual));
  log_error(cds)("JVMTI class retransformation is not supported when archive was generated with -XX:+PreloadSharedClasses.");
  MetaspaceShared::unrecoverable_loading_error();
}

void AOTLoadedClassManager::init_javabase_preloaded_classes(TRAPS) {
  maybe_init_or_link(AOTLoadedClassTable::for_static_archive()->boot(),  CHECK);
  //maybe_init_or_link(_dynamic_aot_loading_list._boot, CHECK); // TODO

  // Initialize java.base classes in the default subgraph.
  HeapShared::initialize_default_subgraph_classes(Handle(), CHECK);
}

void AOTLoadedClassManager::post_module_init(TRAPS) {
  if (!CDSConfig::has_preloaded_classes()) {
    return;
  }

  post_module_init_impl(AOTLoadedClassTable::for_static_archive(), CHECK);
  post_module_init_impl(AOTLoadedClassTable::for_dynamic_archive(), CHECK);
}

void AOTLoadedClassManager::post_module_init_impl(AOTLoadedClassTable* table, TRAPS) {
  // TODO: set the the packages, modules, protection domain, etc, of the
  // boot2 classes ... -- need test case for no -XX:+ArchiveProtectionDomains

  Array<InstanceKlass*>* classes = table->boot2();
  if (classes != nullptr) {
    for (int i = 0; i < classes->length(); i++) {
      InstanceKlass* ik = classes->at(i);
      if (!CDSConfig::is_using_full_module_graph()) {
        // A special case to handle non-FMG when dumping the final archive.
        // We assume that the module graph is exact the same between preimage and final image runs.
        assert(CDSConfig::is_dumping_final_static_archive(), "sanity");

        ik->set_package(ik->class_loader_data(), nullptr, CHECK);

        // See SystemDictionary::load_shared_class_misc
        s2 path_index = ik->shared_classpath_index();
        if (path_index >= 0) { // FIXME ... for lambda form classes
          ik->set_classpath_index(path_index);

          if (CDSConfig::is_dumping_final_static_archive()) {
            if (path_index > ClassLoaderExt::max_used_path_index()) {
              ClassLoaderExt::set_max_used_path_index(path_index);
            }
          }
        }
      }

      ModuleEntry* module_entry = ik->module();
      assert(module_entry != nullptr, "has been restored");
      assert(ik->java_mirror() != nullptr, "has been restored");
      java_lang_Class::set_module(ik->java_mirror(), module_entry->module());
    }

    maybe_init_or_link(classes, CHECK);
  }
}

void AOTLoadedClassManager::maybe_init_or_link(Array<InstanceKlass*>* classes, TRAPS) {
  if (classes != nullptr) {
    for (int i = 0; i < classes->length(); i++) {
      InstanceKlass* ik = classes->at(i);
      if (ik->has_preinitialized_mirror()) {
        ik->initialize_from_cds(CHECK);
      } else if (PrelinkSharedClasses && ik->verified_at_dump_time()) {
        ik->link_class(CHECK);
      }
    }
  }
}

void AOTLoadedClassManager::replay_training_at_init(Array<InstanceKlass*>* classes, TRAPS) {
  if (classes != nullptr) {
    for (int i = 0; i < classes->length(); i++) {
      InstanceKlass* ik = classes->at(i);
      if (ik->has_preinitialized_mirror() && ik->is_initialized() && !ik->has_init_deps_processed()) {
        CompilationPolicy::replay_training_at_init(ik, CHECK);
      }
    }
  }
}

void AOTLoadedClassManager::replay_training_at_init_for_preloaded_classes(TRAPS) {
  if (CDSConfig::has_preloaded_classes() && TrainingData::have_data()) {
    AOTLoadedClassTable* table = AOTLoadedClassTable::for_static_archive(); // not applicable for dynamic archive (?? why??)
    replay_training_at_init(table->boot(),     CHECK);
    replay_training_at_init(table->boot2(),    CHECK);
    replay_training_at_init(table->platform(), CHECK);
    replay_training_at_init(table->app(),      CHECK);

    CompilationPolicy::replay_training_at_init(false, CHECK);
  }
}

void AOTLoadedClassManager::print_counters() {
  if (UsePerfData && _perf_class_preload_counters != nullptr) {
    LogStreamHandle(Info, init) log;
    if (log.is_enabled()) {
      log.print_cr("AOTLoadedClassManager:");
      log.print_cr("  preload:           %ldms (elapsed) %ld (thread) / %ld events",
                   _perf_class_preload_counters->elapsed_counter_value_ms(),
                   _perf_class_preload_counters->thread_counter_value_ms(),
                   _perf_classes_preloaded->get_value());
    }
  }
}
