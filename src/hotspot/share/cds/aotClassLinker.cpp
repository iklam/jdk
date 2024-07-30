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
#include "cds/aotConstantPoolResolver.hpp"
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

AOTClassLinker::ClassesTable* AOTClassLinker::_vm_classes = nullptr;
AOTClassLinker::ClassesTable* AOTClassLinker::_candidates = nullptr;
GrowableArrayCHeap<InstanceKlass*, mtClassShared>* AOTClassLinker::_sorted_candidates = nullptr;

bool AOTClassLinker::is_initialized() {
  assert(CDSConfig::is_dumping_archive(), "AOTClassLinker is for CDS dumping only");
  return _vm_classes != nullptr;
}

void AOTClassLinker::initialize() {
  assert(!is_initialized(), "sanity");

  _vm_classes = new (mtClass)ClassesTable();
  _candidates = new (mtClass)ClassesTable();
  _sorted_candidates = new GrowableArrayCHeap<InstanceKlass*, mtClassShared>(1000);

  for (auto id : EnumRange<vmClassID>{}) {
    add_vm_class(vmClasses::klass_at(id));
  }

  assert(is_initialized(), "sanity");

  AOTConstantPoolResolver::initialize();
}

void AOTClassLinker::dispose() {
  assert(is_initialized(), "sanity");

  delete _vm_classes;
  delete _candidates;
  delete _sorted_candidates;
  _vm_classes = nullptr;
  _candidates = nullptr;
  _sorted_candidates = nullptr;

  assert(!is_initialized(), "sanity");


  AOTConstantPoolResolver::dispose();
}

bool AOTClassLinker::is_vm_class(InstanceKlass* ik) {
  assert(is_initialized(), "sanity");
  return (_vm_classes->get(ik) != nullptr);
}

void AOTClassLinker::add_vm_class(InstanceKlass* ik) {
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

bool AOTClassLinker::is_candidate(InstanceKlass* ik) {
  return (_candidates->get(ik) != nullptr);
}

void AOTClassLinker::add_candidate(InstanceKlass* ik) {
  _candidates->put_when_absent(ik, true);
  _sorted_candidates->append(ik);
}

bool AOTClassLinker::try_add_candidate(InstanceKlass* ik) {
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

void AOTClassLinker::write_to_archive() {
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

  AOTLinkedClassTable* table = AOTLinkedClassTable::get(CDSConfig::is_dumping_static_archive());
  table->set_boot(write_classes(nullptr, true));
  table->set_boot2(write_classes(nullptr, false));
  table->set_platform(write_classes(SystemDictionary::java_platform_loader(), false));
  table->set_app(write_classes(SystemDictionary::java_system_loader(), false));
}

Array<InstanceKlass*>* AOTClassLinker::write_classes(oop class_loader, bool is_javabase) {
  ResourceMark rm;
  GrowableArray<InstanceKlass*> list;

  for (int i = 0; i < _sorted_candidates->length(); i++) {
    InstanceKlass* ik = _sorted_candidates->at(i);
    if (ik->class_loader() != class_loader) {
      continue;
    }
    if ((ik->module() == ModuleEntryTable::javabase_moduleEntry()) != is_javabase) {
      continue;
    }

    if (ik->is_shared() && CDSConfig::is_dumping_dynamic_archive()) {
      if (!CDSConfig::has_preloaded_classes()) {
        list.append(ik);
      }
    } else {
      list.append(ArchiveBuilder::current()->get_buffered_addr(ik));
    }
  }

  if (list.length() == 0) {
    return nullptr;
  } else {
    const char* category = ArchiveUtils::class_category(list.at(0));
    log_info(cds, preload)("written %d class(es) for category %s", list.length(), category);
    return ArchiveUtils::archive_array(&list);
  }
}

int AOTClassLinker::num_platform_initiated_classes() {
  return 0;// FIXME
}

int AOTClassLinker::num_app_initiated_classes() {
  return 0;// FIXME
}
