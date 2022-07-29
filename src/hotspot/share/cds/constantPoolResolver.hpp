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

#ifndef SHARE_CDS_CONSTANTPOOLRESOLVER_HPP
#define SHARE_CDS_CONSTANTPOOLRESOLVER_HPP

#include "memory/allStatic.hpp"
#include "utilities/resourceHash.hpp"

class ConstantPool;
class InstanceKlass;
class Klass;

class ConstantPoolResolver : AllStatic {
  typedef ResourceHashtable<InstanceKlass*, bool, 15889, ResourceObj::C_HEAP, mtClassShared> VmClassesTable;
  static VmClassesTable* _vm_classes_table;

  static void add_one_vm_class(InstanceKlass* ik);
  static bool can_archive_resolved_vm_class(InstanceKlass* cp_holder, InstanceKlass* resolved_klass);
  static bool is_in_archivebuilder_buffer(address p);

  template <typename T>
  static bool is_in_archivebuilder_buffer(T p) {
    return is_in_archivebuilder_buffer((address)(p));
  }

public:
  static void initialize();
  static void free();

  static bool is_vm_class(InstanceKlass* ik);
  static bool can_archive_resolved_klass(ConstantPool* cp, int cp_index);

  class State {
  public:
    State() {
      initialize();
    }
    ~State() {
      free();
    }
  };
};

#endif // SHARE_CDS_CONSTANTPOOLRESOLVER_HPP
