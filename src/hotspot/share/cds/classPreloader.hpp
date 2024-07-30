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

// List of classes to be loaded at VM bootstrap.
class AOTLoadedClasses {
  static AOTLoadedClasses _for_static_archive;
  static AOTLoadedClasses _for_dynamic_archive;

  bool _is_static_archive;
  Array<InstanceKlass*>* _boot;  // only java.base classes
  Array<InstanceKlass*>* _boot2; // boot classes in other modules
  Array<InstanceKlass*>* _platform;
  Array<InstanceKlass*>* _app;

public:
  AOTLoadedClasses(bool is_static_archive) :
    _is_static_archive(is_static_archive),
    _boot(nullptr), _boot2(nullptr),
    _platform(nullptr), _app(nullptr) {}

  static AOTLoadedClasses* for_static_archive()  { return &_for_static_archive; }
  static AOTLoadedClasses* for_dynamic_archive() { return &_for_dynamic_archive; }

  static AOTLoadedClasses* get(bool is_static_archive) {
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

#if 0
  bool is_empty() const {
    return _boot == nullptr && _boot2 == nullptr && _platform == nullptr && _app == nullptr;
  }
#endif

  void serialize(SerializeClosure* soc);
};

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

  static void add_vm_class(InstanceKlass* ik);
  static void add_candidate(InstanceKlass* klasses);
  static Array<InstanceKlass*>* record_preloaded_classes(int loader_type);

  static bool is_in_javabase(InstanceKlass* ik);

  static bool is_initialized();

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

  static int  num_app_initiated_classes();
  static int  num_platform_initiated_classes();
};

class ClassPreloader :  AllStatic {
  static bool _preloading_non_javavase_classes;
  static Array<InstanceKlass*>* _unregistered_classes_from_preimage;

  static void load_initiated_classes(JavaThread* current, const char* category, Handle loader, Array<InstanceKlass*>* preloaded_list);
  static void runtime_preload(AOTLoadedClasses* table, Handle loader, TRAPS);
  static void runtime_preload(Array<InstanceKlass*>* preloaded_classes, const char* category, Handle loader, TRAPS);
  static void runtime_preload_class_quick(InstanceKlass* ik, ClassLoaderData* loader_data, Handle domain, TRAPS);
  static void preload_archived_hidden_class(Handle class_loader, InstanceKlass* ik, TRAPS);
  static void post_module_init_impl(AOTLoadedClasses* table, TRAPS);
  static void maybe_init_or_link(Array<InstanceKlass*>* preloaded_classes, TRAPS);
  static void jvmti_agent_error(InstanceKlass* expected, InstanceKlass* actual, const char* type);

  static bool is_in_javabase(InstanceKlass* ik);

  static void replay_training_at_init(Array<InstanceKlass*>* preloaded_classes, TRAPS) NOT_CDS_RETURN;

public:
  static void serialize(SerializeClosure* soc, bool is_static_archive);
  static void record_unregistered_classes();

  static void runtime_preload(JavaThread* current, Handle loader) NOT_CDS_RETURN;
  static bool is_preloading_non_javavase_classes() NOT_CDS_RETURN_(false);
  static void init_javabase_preloaded_classes(TRAPS) NOT_CDS_RETURN;
  static void post_module_init(TRAPS) NOT_CDS_RETURN;
  static void replay_training_at_init_for_preloaded_classes(TRAPS) NOT_CDS_RETURN;
  static bool class_preloading_finished();

  static void print_counters() NOT_CDS_RETURN;
};

#endif // SHARE_CDS_CLASSPRELOADER_HPP
