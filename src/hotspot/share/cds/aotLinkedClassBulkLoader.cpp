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
#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/classLoaderExt.hpp"
#include "classfile/dictionary.hpp"
#include "classfile/javaClasses.hpp"
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
ClassLoaderData* AOTLinkedClassBulkLoader::_platform_class_loader_data = nullptr;
ClassLoaderData* AOTLinkedClassBulkLoader::_app_class_loader_data = nullptr;

static int _platform_loader_root_index;
static int _app_loader_root_index;

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

#if INCLUDE_CDS_JAVA_HEAP
void AOTLinkedClassBulkLoader::record_heap_roots() {
  if (CDSConfig::is_dumping_full_module_graph() && PreloadSharedClasses) {
    _platform_loader_root_index = HeapShared::append_root(SystemDictionary::java_platform_loader());
    _app_loader_root_index = HeapShared::append_root(SystemDictionary::java_system_loader());
  }
}
#endif

void AOTLinkedClassBulkLoader::serialize(SerializeClosure* soc, bool is_static_archive) {
  AOTLinkedClassTable::get(is_static_archive)->serialize(soc);

  if (is_static_archive) {
    soc->do_ptr((void**)&_unregistered_classes_from_preimage);
    soc->do_int(&_platform_loader_root_index);
    soc->do_int(&_app_loader_root_index);

    if (soc->reading() && UsePerfData) {
      JavaThread* THREAD = JavaThread::current();
      NEWPERFEVENTCOUNTER(_perf_classes_preloaded, SUN_CLS, "preloadedClasses");
      NEWPERFTICKCOUNTERS(_perf_class_preload_counters, SUN_CLS, "classPreload");
    }
  }
}

volatile bool _class_preloading_finished = false;

bool AOTLinkedClassBulkLoader::class_preloading_finished() {
  if (!CDSConfig::has_preloaded_classes()) {
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

Handle AOTLinkedClassBulkLoader::init_platform_loader(JavaThread* current)  {
  Handle platform_loader(current, HeapShared::get_root(_platform_loader_root_index));
  ClassLoaderData* platform_loader_data = SystemDictionary::register_loader(platform_loader);
  SystemDictionary::set_platform_loader(platform_loader_data);
  return platform_loader;  
}

Handle AOTLinkedClassBulkLoader::init_app_loader(JavaThread* current)  {
  Handle app_loader(current, HeapShared::get_root(_app_loader_root_index));
  ClassLoaderData* app_loader_data = SystemDictionary::register_loader(app_loader);
  SystemDictionary::set_system_loader(app_loader_data);
  return app_loader;  
}

static ClassLoaderData* loader_data(Handle class_loader) {
  return java_lang_ClassLoader::loader_data(class_loader());
}

static ClassLoaderData* create_loader_data() {
  return ClassLoaderDataGraph::add_for_leyden();
}

void AOTLinkedClassBulkLoader::restore_class_loader_data(Handle loader) {
  assert(CDSConfig::is_dumping_final_static_archive(), "sanity");
  ClassLoaderData* loader_data = java_lang_ClassLoader::loader_data(loader());
  if (loader_data == nullptr) {
    Klass* loader_class = loader()->klass();
    if (loader_class == vmClasses::jdk_internal_loader_ClassLoaders_PlatformClassLoader_klass()) {
      _platform_class_loader_data->update_class_loader(loader);
      java_lang_ClassLoader::release_set_loader_data(loader(), _platform_class_loader_data);
      SystemDictionary::set_platform_loader(_platform_class_loader_data);
    }
    if (loader_class == vmClasses::jdk_internal_loader_ClassLoaders_AppClassLoader_klass()) {
      _app_class_loader_data->update_class_loader(loader);
      java_lang_ClassLoader::release_set_loader_data(loader(), _app_class_loader_data);
      SystemDictionary::set_system_loader(_app_class_loader_data);
    }
  }
}

void AOTLinkedClassBulkLoader::load(JavaThread* current) {
  if (CDSConfig::has_preloaded_classes()) {
    HandleMark hm(current);
    ResourceMark rm(current);
    ExceptionMark em(current);

    if (CDSConfig::is_dumping_final_static_archive()) {
      _platform_class_loader_data = create_loader_data();
      _app_class_loader_data = create_loader_data();

      load_impl(LoaderKind::BOOT,     ClassLoaderData::the_null_class_loader_data(), current); // cannot throw
      load_impl(LoaderKind::PLATFORM, _platform_class_loader_data, current);                   // cannot throw
      load_impl(LoaderKind::APP,      _app_class_loader_data, current);                        // cannot throw


    } else {
      load_impl(LoaderKind::BOOT,     ClassLoaderData::the_null_class_loader_data(), current); // cannot throw
      load_impl(LoaderKind::PLATFORM, loader_data(init_platform_loader(current)), current);    // cannot throw
      load_impl(LoaderKind::APP,      loader_data(init_app_loader(current)), current);         // cannot throw
    }
  }

  assert(!current->has_pending_exception(), "VM should have exited due to ExceptionMark");

#if 0
  // Hmm, does JavacBench hang here??
  if (VerifyDuringStartup) {
    VM_Verify verify_op;
    VMThread::execute(&verify_op);
  }
#endif
}

void AOTLinkedClassBulkLoader::load_impl(LoaderKind loader_kind, ClassLoaderData* loader_data, TRAPS) {
  load_table(AOTLinkedClassTable::for_static_archive(),  loader_kind, loader_data, CHECK);
  load_table(AOTLinkedClassTable::for_dynamic_archive(), loader_kind, loader_data, CHECK);
}

/*

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
*/


void AOTLinkedClassBulkLoader::load_table(AOTLinkedClassTable* table, LoaderKind loader_kind, ClassLoaderData* loader_data, TRAPS) {
  PerfTraceTime timer(_perf_class_preload_counters);

  // ResourceMark is missing in the code below due to JDK-8307315
  ResourceMark rm(THREAD);
  switch (loader_kind) {
  case LoaderKind::BOOT:
    {
      load_classes(table->boot(), "boot ", loader_data, CHECK);

      _preloading_non_javavase_classes = true;
      load_classes(table->boot2(), "boot2", loader_data, CHECK);
      _preloading_non_javavase_classes = false;
    }
    break;
  case LoaderKind::PLATFORM:
    {
      const char* category = "plat ";
      load_initiated_classes(THREAD, category, loader_data, table->boot());
      load_initiated_classes(THREAD, category, loader_data, table->boot2());

      _preloading_non_javavase_classes = true;
      load_classes(table->platform(), category, loader_data, CHECK);
      _preloading_non_javavase_classes = false;
    }
    break;
  case LoaderKind::APP:
    {
      const char* category = "app  ";
      load_initiated_classes(THREAD, category, loader_data, table->boot());
      load_initiated_classes(THREAD, category, loader_data, table->boot2());
      load_initiated_classes(THREAD, category, loader_data, table->platform());

      _preloading_non_javavase_classes = true;
      load_classes(table->app(), category, loader_data, CHECK);
      _preloading_non_javavase_classes = false;
    }
  }
}

void AOTLinkedClassBulkLoader::load_classes(Array<InstanceKlass*>* classes, const char* category, ClassLoaderData *loader_data, TRAPS) {
  if (classes == nullptr) {
    return;
  }

  for (int i = 0; i < classes->length(); i++) {
    if (UsePerfData) {
      _perf_classes_preloaded->inc();
    }
    InstanceKlass* ik = classes->at(i);
    if (log_is_enabled(Info, cds, preload)) {
      ResourceMark rm;
      log_info(cds, preload)("%s %s%s%s", category, ik->external_name(),
                             ik->is_loaded() ? " (already loaded)" : "",
                             ik->is_hidden() ? " (hidden)" : "");
    }

    if (!ik->is_loaded()) {
      if (ik->is_hidden()) {
        load_hidden_class(loader_data, ik, CHECK);
      } else {
        load_class_quick(ik, loader_data, Handle(), CHECK);
      }
    }
  }
}

void AOTLinkedClassBulkLoader::load_initiated_classes(JavaThread* current, const char* category,
                                                      ClassLoaderData* loader_data, Array<InstanceKlass*>* classes) {
  if (classes == nullptr) {
    return;
  }

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
void AOTLinkedClassBulkLoader::load_hidden_class(ClassLoaderData* loader_data, InstanceKlass* ik, TRAPS) {
  DEBUG_ONLY({
      assert(ik->super() == vmClasses::Object_klass(), "must be");
      for (int i = 0; i < ik->local_interfaces()->length(); i++) {
        assert(ik->local_interfaces()->at(i)->is_loaded(), "must be");
      }
    });

  ik->restore_unshareable_info(loader_data, Handle(), NULL, CHECK);
  SystemDictionary::load_shared_class_misc(ik, loader_data);
  ik->add_to_hierarchy(THREAD);
  assert(ik->is_loaded(), "Must be in at least loaded state");
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

  post_module_init_impl(AOTLinkedClassTable::for_static_archive() ->boot2(), CHECK);
  post_module_init_impl(AOTLinkedClassTable::for_dynamic_archive()->boot2(), CHECK);

  post_module_init_impl(AOTLinkedClassTable::for_static_archive() ->platform(), CHECK);
  post_module_init_impl(AOTLinkedClassTable::for_dynamic_archive()->platform(), CHECK);

  post_module_init_impl(AOTLinkedClassTable::for_static_archive() ->app(), CHECK);
  post_module_init_impl(AOTLinkedClassTable::for_dynamic_archive()->app(), CHECK);

  // TODO: do we support subgraph classes for boot2??
  Handle h_platform_loader(THREAD, SystemDictionary::java_platform_loader());
  Handle h_system_loader(THREAD, SystemDictionary::java_system_loader());
  HeapShared::initialize_default_subgraph_classes(h_platform_loader, CHECK);
  HeapShared::initialize_default_subgraph_classes(h_system_loader, CHECK);

  Atomic::release_store(&_class_preloading_finished, true);
}

void AOTLinkedClassBulkLoader::post_module_init_impl(Array<InstanceKlass*>* classes, TRAPS) {
  if (classes == nullptr) {
    return;
  }

  for (int i = 0; i < classes->length(); i++) {
    InstanceKlass* ik = classes->at(i);
    Handle protection_domain;
    Handle class_loader(THREAD, ik->class_loader());
    if (class_loader() != nullptr) {
      SharedClassLoadingMark slm(THREAD, ik);
      PackageEntry* pkg_entry = CDSProtectionDomain::get_package_entry_from_class(ik, class_loader);
      if (!ik->name()->starts_with("jdk/proxy")) { // java/lang/reflect/Proxy$ProxyBuilder defines the proxy classes with a null protection domain.
        protection_domain = CDSProtectionDomain::init_security_info(class_loader, ik, pkg_entry, CHECK);
      }
    }

    ik->restore_java_mirror(ik->class_loader_data(), protection_domain, CHECK);

    if (!CDSConfig::is_using_full_module_graph()) {
      // A special case to handle non-FMG when dumping the final archive.
      // We assume that the module graph is exact the same between preimage and final image runs.
      assert(CDSConfig::is_dumping_final_static_archive(), "sanity");

      ik->set_package(ik->class_loader_data(), nullptr, CHECK);

    }

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

    ModuleEntry* module_entry = ik->module();
    assert(module_entry != nullptr, "has been restored");
    assert(ik->java_mirror() != nullptr, "has been restored");
    java_lang_Class::set_module(ik->java_mirror(), module_entry->module());
  }

  maybe_init_or_link(classes, CHECK);
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
