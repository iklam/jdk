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
#include "classfile/systemDictionary.hpp"
#include "classfile/vmClasses.hpp"
#include "memory/resourceArea.hpp"
#include "oops/constantPool.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "runtime/handles.inline.hpp"

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
  if (!Thread::current()->is_VM_thread() || ArchiveBuilder::current() == NULL) {
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

// TODO -- allow objArray classes, too
bool ConstantPoolResolver::can_archive_resolved_klass(InstanceKlass* cp_holder, Klass* resolved_klass) {
  assert(!is_in_archivebuilder_buffer(cp_holder), "sanity");
  assert(!is_in_archivebuilder_buffer(resolved_klass), "sanity");

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

bool ConstantPoolResolver::can_archive_resolved_klass(ConstantPool* cp, int cp_index) {
  assert(!is_in_archivebuilder_buffer(cp), "sanity");
  assert(cp->tag_at(cp_index).is_klass(), "must be resolved");

  InstanceKlass* cp_holder = cp->pool_holder();

  CPKlassSlot kslot = cp->klass_slot_at(cp_index);
  int resolved_klass_index = kslot.resolved_klass_index();
  Klass* resolved_klass = cp->resolved_klasses()->at(resolved_klass_index);
  assert(resolved_klass != NULL, "must be");

  return can_archive_resolved_klass(cp_holder, resolved_klass);
}

void ConstantPoolResolver::dumptime_resolve(InstanceKlass* ik, TRAPS) {
  constantPoolHandle cp(THREAD, ik->constants());
  if (cp->cache() == NULL || cp->reference_map() == NULL) {
    // The cache may be NULL if the pool_holder klass fails verification
    // at dump time due to missing dependencies.
    return;
  }

  bool first_time;
  _processed_classes->put_if_absent(ik, &first_time);
  if (!first_time) {
    return;
  }

  for (int cp_index = 1; cp_index < cp->length(); cp_index++) { // Index 0 is unused
    switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_UnresolvedClass:
      maybe_resolve_class(cp, cp_index, CHECK);
      break;

    case JVM_CONSTANT_String:
      resolve_string(cp, cp_index, CHECK); // may throw OOM when interning strings.
      break;
    }
  }
}

Klass* ConstantPoolResolver::find_loaded_class(JavaThread* THREAD, oop class_loader, Symbol* name) {
  Handle h_loader(THREAD, class_loader);
  Klass* k = SystemDictionary::find_instance_or_array_klass(name,
                                                            h_loader,
                                                            Handle());
  if (k != NULL) {
    return k;
  }
  if (class_loader == SystemDictionary::java_system_loader()) {
    return find_loaded_class(THREAD, SystemDictionary::java_platform_loader(), name);
  } else if (class_loader == SystemDictionary::java_platform_loader()) {
    return find_loaded_class(THREAD, NULL, name);
  }

  return NULL;
}

void ConstantPoolResolver::maybe_resolve_class(constantPoolHandle cp, int cp_index, TRAPS) {
  InstanceKlass* cp_holder = cp->pool_holder();
  if (!cp_holder->is_shared_boot_class() &&
      !cp_holder->is_shared_platform_class() &&
      !cp_holder->is_shared_app_class()) {
    // Don't trust custom loaders, as they may not be well-behaved
    // when resolving classes.
    //
    // TODO: we should be able to trust the supertypes of cp_holder.
    return;
  }

  CPKlassSlot kslot = cp->klass_slot_at(cp_index);
  int name_index = kslot.name_index();
  Symbol* name = cp->symbol_at(name_index);
  Klass* resolved_klass = find_loaded_class(THREAD, cp_holder->class_loader(), name);
  if (resolved_klass != NULL && can_archive_resolved_klass(cp_holder, resolved_klass)) {
    Klass* k = ConstantPool::klass_at_impl(cp, cp_index, CHECK); // Should fail only with OOM
    assert(k == resolved_klass, "must be");
  }
}


#if INCLUDE_CDS_JAVA_HEAP
void ConstantPoolResolver::resolve_string(constantPoolHandle cp, int cp_index, TRAPS) {
  if (!DumpSharedSpaces) {
    // The archive heap is not supported for the dynamic archive.
    return;
  }

  int cache_index = cp->cp_to_object_index(cp_index);
  ConstantPool::string_at_impl(cp, cp_index, cache_index, CHECK);

  //ResourceMark rm;
  //tty->print_cr("RESOLVE %s %d %d", cp->pool_holder()->external_name(), cp_index, cache_index);
}
#endif

