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
#include "cds/archiveBuilder.hpp"
#include "cds/constantPoolResolver.hpp"
#include "classfile/vmClasses.hpp"
#include "oops/constantPool.hpp"
#include "oops/instanceKlass.hpp"

ConstantPoolResolver::ClassesTable* ConstantPoolResolver::_processed_classes = NULL;
ConstantPoolResolver::ClassesTable* ConstantPoolResolver::_vm_classes = NULL;

bool ConstantPoolResolver::is_vm_class(InstanceKlass* ik) {
  return (_vm_classes->get(ik) != NULL);
}

void ConstantPoolResolver::add_one_vm_class(InstanceKlass* ik) {
  bool created;
  _vm_classes->put_if_absent(ik, &created);
  if (created) {
    InstanceKlass* super = ik->java_super();
    if (super != NULL) {
      add_one_vm_class(super);
    }
    Array<InstanceKlass*>* ifs = ik->local_interfaces();
    for (int i = 0; i < ifs->length(); i++) {
      add_one_vm_class(ifs->at(i));
    }
  }
}

void ConstantPoolResolver::initialize() {
  assert(_processed_classes == NULL, "must be");
  _processed_classes = new (ResourceObj::C_HEAP, mtClassShared) ClassesTable();

  assert(_vm_classes == NULL, "must be");
  _vm_classes = new (ResourceObj::C_HEAP, mtClassShared) ClassesTable();

  for (auto id : EnumRange<vmClassID>{}) {
    add_one_vm_class(vmClasses::klass_at(id));
  }
}

void ConstantPoolResolver::free() {
  assert(_processed_classes != NULL, "must be");
  delete _processed_classes;
  _processed_classes = NULL;

  assert(_vm_classes != NULL, "must be");
  delete _vm_classes;
  _vm_classes = NULL;
}

bool ConstantPoolResolver::is_in_archivebuilder_buffer(address p) {
  if (ArchiveBuilder::current() == NULL) {
    return false;
  } else {
    return ArchiveBuilder::current()->is_in_buffer_space(p);
  }
}

bool ConstantPoolResolver::can_archive_resolved_vm_class(InstanceKlass* cp_holder, InstanceKlass* resolved_klass) {
  if (!ConstantPoolResolver::is_vm_class(resolved_klass)) {
    return false;
  }
  if (!cp_holder->is_shared_boot_class() &&
      !cp_holder->is_shared_platform_class() &&
      !cp_holder->is_shared_app_class()) {
    // Custom loaders are not guaranteed to resolve the vmClasses to the
    // ones resolved by the boot loader.
    return false;
  }
  if (cp_holder->class_loader_data() != resolved_klass->class_loader_data()) {
    // If they are loaded by different loaders, it's possible for cp_holder
    // to be loaded, but resolved_klass is still not resolved in
    // cp_holder->class_loader().

    // TODO: this check can be removed if we preload the vmClasses into
    // platform and app loaders during VM bootstrap.
    return false;
  }

  return true;
}

bool ConstantPoolResolver::can_archive_resolved_klass(ConstantPool* cp, int cp_index) {
  assert(!is_in_archivebuilder_buffer(cp), "sanity");
  assert(cp->tag_at(cp_index).is_klass(), "must be resolved");

  InstanceKlass* cp_holder = cp->pool_holder();

  CPKlassSlot kslot = cp->klass_slot_at(cp_index);
  int resolved_klass_index = kslot.resolved_klass_index();
  Klass* resolved_klass = cp->resolved_klasses()->at(resolved_klass_index);
  assert(resolved_klass != NULL, "must be");

  if (resolved_klass->is_instance_klass()) {
    InstanceKlass* ik = InstanceKlass::cast(resolved_klass);
    if (can_archive_resolved_vm_class(cp_holder, ik)) {
      return true;
    }

    if (cp_holder->is_subtype_of(ik)) {
      // All super types of ik will be resolved in ik->class_loader() before
      // ik is defined in this loader, so it's safe to archive the resolved klass reference.
      return true;
    }
  }

  return false;
}

#if INCLUDE_CDS_JAVA_HEAP
void ConstantPoolResolver::dumptime_resolve_strings(InstanceKlass* ik, TRAPS) {
  if (!DumpSharedSpaces) {
    // The archive heap is not supported for the dynamic archive.
    return;
  }

  ConstantPool* cp = ik->constants();
  // The _cache may be NULL if the _pool_holder klass fails verification
  // at dump time due to missing dependencies.
  if (cp->cache() == NULL || cp->reference_map() == NULL) {
    return; // nothing to do
  }

  constantPoolHandle cph(THREAD, cp);
  for (int index = 1; index < cp->length(); index++) { // Index 0 is unused
    if (cp->tag_at(index).is_string()) {
      int cache_index = cph->cp_to_object_index(index);
      cp->string_at_impl(cph, index, cache_index, CHECK);
    }
  }
}
#endif

void ConstantPoolResolver::dumptime_resolve(InstanceKlass* ik, TRAPS) {
  bool first_time;
  _processed_classes->put_if_absent(ik, &first_time);

  if (first_time) {
    dumptime_resolve_strings(ik, CHECK); // may throw OOM when interning strings.
  }
}
