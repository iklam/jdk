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

#ifndef SHARE_CDS_CLASSPRELOADER_HPP
#define SHARE_CDS_CLASSPRELOADER_HPP

#include "interpreter/bytecodes.hpp"
#include "oops/oopsHierarchy.hpp"
#include "memory/allStatic.hpp"
#include "memory/allocation.hpp"
#include "runtime/handles.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/resourceHash.hpp"

class InstanceKlass;
class Klass;
class SerializeClosure;
template <typename T> class Array;

// Table of classes to be loaded at VM bootstrap. A JVM could use up to
// two such tables (one for static archive, one for dynamic archive)/
class AOTLoadedClassTable;

// Decides which classes should be loaded at VM bootstrap.
// (Used only when dumping CDS archive)
class AOTLoadedClassRecorder :  AllStatic {
  using ClassesTable = ResourceHashtable<InstanceKlass*, bool, 15889, AnyObj::C_HEAP, mtClassShared>;

  // Classes loaded inside vmClasses::resolve_all()
  static ClassesTable* _vm_classes;

  // Classes that should be automatically loaded into system dictionary at VM start-up
  static ClassesTable* _candidates;

  // Sorted list such that super types come first.
  static GrowableArrayCHeap<InstanceKlass*, mtClassShared>* _sorted_candidates;

  static bool is_initialized(); // for debugging

  static void add_vm_class(InstanceKlass* ik);
  static void add_candidate(InstanceKlass* ik);

  static Array<InstanceKlass*>* write_classes(oop class_loader, bool is_javabase);

public:
  static void initialize();
  static void write_to_archive();
  static void dispose();

  // Is this class resolved as part of vmClasses::resolve_all()?
  static bool is_vm_class(InstanceKlass* ik);

  // When CDS is enabled, is ik guatanteed to be loaded at deployment time (and
  // cannot be replaced by JVMTI, etc)?
  // This is a necessary (not but sufficient) condition for keeping a direct pointer
  // to ik in precomputed data (such as ConstantPool entries in archived classes,
  // or in AOT-compiled code).
  static bool is_candidate(InstanceKlass* ik);

  // Request that ik to be added to the candidates table. This will return succeed only if
  // ik is allowed to be aot-loaded.
  static bool try_add_candidate(InstanceKlass* ik);

  static int num_app_initiated_classes();
  static int num_platform_initiated_classes();
};

class AOTLoadedClassManager :  AllStatic {
  static bool _preloading_non_javavase_classes;
  static Array<InstanceKlass*>* _unregistered_classes_from_preimage;

  static void load_table(AOTLoadedClassTable* table, Handle loader, TRAPS);
  static void load_classes(Array<InstanceKlass*>* classes, const char* category, Handle loader, TRAPS);
  static void load_class_quick(InstanceKlass* ik, ClassLoaderData* loader_data, Handle domain, TRAPS);
  static void load_initiated_classes(JavaThread* current, const char* category, Handle loader, Array<InstanceKlass*>* classes);
  static void load_hidden_class(Handle class_loader, InstanceKlass* ik, TRAPS);
  static void post_module_init_impl(AOTLoadedClassTable* table, TRAPS);
  static void maybe_init_or_link(Array<InstanceKlass*>* classes, TRAPS);
  static void jvmti_agent_error(InstanceKlass* expected, InstanceKlass* actual, const char* type);

  static void replay_training_at_init(Array<InstanceKlass*>* classes, TRAPS) NOT_CDS_RETURN;

public:
  static void serialize(SerializeClosure* soc, bool is_static_archive);
  static void record_unregistered_classes();

  static void load(JavaThread* current, Handle loader) NOT_CDS_RETURN;
  static bool is_preloading_non_javavase_classes() NOT_CDS_RETURN_(false);
  static void init_javabase_preloaded_classes(TRAPS) NOT_CDS_RETURN;
  static void post_module_init(TRAPS) NOT_CDS_RETURN;
  static void replay_training_at_init_for_preloaded_classes(TRAPS) NOT_CDS_RETURN;
  static bool class_preloading_finished();

  static void print_counters() NOT_CDS_RETURN;
};

using ClassPreloader = AOTLoadedClassManager;

#endif // SHARE_CDS_CLASSPRELOADER_HPP
