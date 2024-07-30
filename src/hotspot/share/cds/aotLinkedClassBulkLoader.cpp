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
#include "cds/aotClassLinker.hpp"
#include "cds/aotLinkedClassBulkLoader.hpp"
#include "cds/aotLinkedClassTable.hpp"
#include "cds/archiveBuilder.hpp"
#include "cds/archiveUtils.inline.hpp"
#include "cds/cdsAccess.hpp"
#include "cds/cdsConfig.hpp"
#include "cds/cdsProtectionDomain.hpp"
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

Array<InstanceKlass*>* AOTLinkedClassBulkLoader::_unregistered_classes_from_preimage = nullptr;
bool AOTLinkedClassBulkLoader::_preloading_non_javavase_classes = false;

static PerfCounter* _perf_classes_preloaded = nullptr;
static PerfTickCounters* _perf_class_preload_counters = nullptr;

void AOTLinkedClassBulkLoader::record_unregistered_classes() {
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

void AOTLinkedClassBulkLoader::serialize(SerializeClosure* soc, bool is_static_archive) {
  AOTLinkedClassTable::get(is_static_archive)->serialize(soc);

  if (is_static_archive) {
    soc->do_ptr((void**)&_unregistered_classes_from_preimage);
  }

  if (is_static_archive && soc->reading() && UsePerfData) {
    JavaThread* THREAD = JavaThread::current();
    NEWPERFEVENTCOUNTER(_perf_classes_preloaded, SUN_CLS, "preloadedClasses");
    NEWPERFTICKCOUNTERS(_perf_class_preload_counters, SUN_CLS, "classPreload");
  }
}

volatile bool _class_preloading_finished = false;

bool AOTLinkedClassBulkLoader::class_preloading_finished() {
  if (!UseSharedSpaces) {
    return true;
  } else {
    // The ConstantPools of preloaded classes have references to other preloaded classes. We don't
    // want any Java code (including JVMCI compiler) to use these classes until all of them
    // are loaded.
    return Atomic::load_acquire(&_class_preloading_finished);
  }
}

bool AOTLinkedClassBulkLoader::is_preloading_non_javavase_classes() { // FIXME -- need a better name
  return !Universe::is_fully_initialized() && _preloading_non_javavase_classes;
}

void AOTLinkedClassBulkLoader::load(JavaThread* current, Handle loader) {
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
    load_table(AOTLinkedClassTable::for_static_archive(),  loader, current); // cannot throw
    load_table(AOTLinkedClassTable::for_dynamic_archive(), loader, current); // cannot throw

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

void AOTLinkedClassBulkLoader::load_table(AOTLinkedClassTable* table, Handle loader, TRAPS) {
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

void AOTLinkedClassBulkLoader::load_classes(Array<InstanceKlass*>* classes, const char* category, Handle loader, TRAPS) {
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

void AOTLinkedClassBulkLoader::load_initiated_classes(JavaThread* current, const char* category, Handle loader, Array<InstanceKlass*>* classes) {
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
void AOTLinkedClassBulkLoader::load_hidden_class(Handle class_loader, InstanceKlass* ik, TRAPS) {
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

void AOTLinkedClassBulkLoader::load_class_quick(InstanceKlass* ik, ClassLoaderData* loader_data, Handle domain, TRAPS) {
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

void AOTLinkedClassBulkLoader::jvmti_agent_error(InstanceKlass* expected, InstanceKlass* actual, const char* type) {
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

void AOTLinkedClassBulkLoader::init_javabase_preloaded_classes(TRAPS) {
  maybe_init_or_link(AOTLinkedClassTable::for_static_archive()->boot(),  CHECK);
  //maybe_init_or_link(_dynamic_aot_loading_list._boot, CHECK); // TODO

  // Initialize java.base classes in the default subgraph.
  HeapShared::initialize_default_subgraph_classes(Handle(), CHECK);
}

void AOTLinkedClassBulkLoader::post_module_init(TRAPS) {
  if (!CDSConfig::has_preloaded_classes()) {
    return;
  }

  post_module_init_impl(AOTLinkedClassTable::for_static_archive(), CHECK);
  post_module_init_impl(AOTLinkedClassTable::for_dynamic_archive(), CHECK);
}

void AOTLinkedClassBulkLoader::post_module_init_impl(AOTLinkedClassTable* table, TRAPS) {
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

void AOTLinkedClassBulkLoader::maybe_init_or_link(Array<InstanceKlass*>* classes, TRAPS) {
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

void AOTLinkedClassBulkLoader::replay_training_at_init(Array<InstanceKlass*>* classes, TRAPS) {
  if (classes != nullptr) {
    for (int i = 0; i < classes->length(); i++) {
      InstanceKlass* ik = classes->at(i);
      if (ik->has_preinitialized_mirror() && ik->is_initialized() && !ik->has_init_deps_processed()) {
        CompilationPolicy::replay_training_at_init(ik, CHECK);
      }
    }
  }
}

void AOTLinkedClassBulkLoader::replay_training_at_init_for_preloaded_classes(TRAPS) {
  if (CDSConfig::has_preloaded_classes() && TrainingData::have_data()) {
    AOTLinkedClassTable* table = AOTLinkedClassTable::for_static_archive(); // not applicable for dynamic archive (?? why??)
    replay_training_at_init(table->boot(),     CHECK);
    replay_training_at_init(table->boot2(),    CHECK);
    replay_training_at_init(table->platform(), CHECK);
    replay_training_at_init(table->app(),      CHECK);

    CompilationPolicy::replay_training_at_init(false, CHECK);
  }
}

void AOTLinkedClassBulkLoader::print_counters() {
  if (UsePerfData && _perf_class_preload_counters != nullptr) {
    LogStreamHandle(Info, init) log;
    if (log.is_enabled()) {
      log.print_cr("AOTLinkedClassBulkLoader:");
      log.print_cr("  preload:           %ldms (elapsed) %ld (thread) / %ld events",
                   _perf_class_preload_counters->elapsed_counter_value_ms(),
                   _perf_class_preload_counters->thread_counter_value_ms(),
                   _perf_classes_preloaded->get_value());
    }
  }
}
