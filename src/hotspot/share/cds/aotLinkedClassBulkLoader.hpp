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

#ifndef SHARE_CDS_AOTLINKEDCLASSBULKLOADER_HPP
#define SHARE_CDS_AOTLINKEDCLASSBULKLOADER_HPP

#include "memory/allStatic.hpp"
#include "memory/allocation.hpp"
#include "runtime/handles.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"

class AOTLinkedClassTable;
class ClassLoaderData;
class InstanceKlass;
class SerializeClosure;
template <typename T> class Array;

// During a Production Run, the AOTLinkedClassBulkLoader loads all classes from
// a AOTLinkedClassTable into their respective ClassLoaders. This happens very early
// in the JVM bootstrap stage, way before any application code is executed.
//
class AOTLinkedClassBulkLoader :  AllStatic {
  static bool _preloading_non_javavase_classes;
  static Array<InstanceKlass*>* _unregistered_classes_from_preimage;

  static void load_table(AOTLinkedClassTable* table, Handle loader, TRAPS);
  static void load_classes(Array<InstanceKlass*>* classes, const char* category, Handle loader, TRAPS);
  static void load_class_quick(InstanceKlass* ik, ClassLoaderData* loader_data, Handle domain, TRAPS);
  static void load_initiated_classes(JavaThread* current, const char* category, Handle loader, Array<InstanceKlass*>* classes);
  static void load_hidden_class(Handle class_loader, InstanceKlass* ik, TRAPS);
  static void post_module_init_impl(AOTLinkedClassTable* table, TRAPS);
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

#endif // SHARE_CDS_AOTLINKEDCLASSBULKLOADER_HPP
