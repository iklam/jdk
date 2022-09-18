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

#ifndef SHARE_CDS_CLASSPRELINKER_HPP
#define SHARE_CDS_CLASSPRELINKER_HPP

#include "interpreter/bytecodes.hpp"
#include "memory/allocation.hpp"
#include "oops/oopsHierarchy.hpp"
#include "runtime/handles.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"
#include "utilities/resourceHash.hpp"

class ConstantPool;
class constantPoolHandle;
class InstanceKlass;
class Klass;

class ClassPrelinker :  public StackObj {
  typedef ResourceHashtable<InstanceKlass*, bool, 15889, ResourceObj::C_HEAP, mtClassShared> ClassesTable;
  ClassesTable _processed_classes;
  ClassesTable _vm_classes;

  void add_one_vm_class(InstanceKlass* ik);
  bool can_archive_resolved_vm_class(InstanceKlass* cp_holder, InstanceKlass* resolved_klass);

#ifdef ASSERT
  static bool is_in_archivebuilder_buffer(address p);
#endif

  template <typename T>
  static bool is_in_archivebuilder_buffer(T p) {
    return is_in_archivebuilder_buffer((address)(p));
  }

  void resolve_string(constantPoolHandle cp, int cp_index, TRAPS) NOT_CDS_JAVA_HEAP_RETURN;
  Klass* maybe_resolve_class(constantPoolHandle cp, int cp_index, TRAPS);
  void maybe_resolve_field(InstanceKlass* ik, Method* m, Bytecodes::Code bytecode, int cpc_index, TRAPS);
  bool can_archive_resolved_klass(InstanceKlass* cp_holder, Klass* resolved_klass);
  Klass* find_loaded_class(JavaThread* THREAD, oop class_loader, Symbol* name);

  static ClassPrelinker* _singleton;
public:
  ClassPrelinker();
  ~ClassPrelinker();
  static ClassPrelinker* current() {
    assert(_singleton != NULL, "must have one");
    return _singleton;
  }
  bool is_vm_class(InstanceKlass* ik);
  void dumptime_resolve(InstanceKlass* ik, TRAPS);
  Klass* get_resolved_klass_or_null(ConstantPool* cp, int cp_index);
  bool can_archive_resolved_klass(ConstantPool* cp, int cp_index);
  bool can_archive_resolved_field(ConstantPool* cp, int cp_index);
};

#endif // SHARE_CDS_CLASSPRELINKER_HPP
