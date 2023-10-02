/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_CDSCONFIG_HPP
#define SHARE_CDS_CDSCONFIG_HPP

#include "memory/allStatic.hpp"
//#include "utilities/debug.hpp"
#include "utilities/macros.hpp"

class CDSConfig : public AllStatic {
  static bool      _enable_dumping_full_module_graph;
  static bool      _enable_loading_full_module_graph;
  static bool      _has_preloaded_classes;
  static bool      _is_loading_invokedynamic;
public:
  static bool      is_using_dumptime_tables();
  static bool      is_dumping_archive(); // dynamic or static archive
  static bool      is_dumping_static_archive();
  static bool      is_dumping_final_static_archive(); // new "half step" dumping. See ../../../../doc/leyden/xxxxxx.md
  static bool      is_dumping_dynamic_archive();
  static bool      is_dumping_heap();
  static bool      is_loading_heap();
  static void disable_dumping_full_module_graph(const char* reason = nullptr);
  static bool      is_dumping_full_module_graph();
  static void disable_loading_full_module_graph(const char* reason = nullptr);
  static bool      is_loading_full_module_graph();
  static bool      is_dumping_invokedynamic();
  static bool      is_loading_invokedynamic();
  static void  set_is_loading_invokedynamic() { _is_loading_invokedynamic = true; }
  static bool      is_dumping_cached_code();
  static void disable_dumping_cached_code();
  static void  enable_dumping_cached_code();
  static bool      is_initing_classes_at_dump_time();
  static bool      has_preloaded_classes()     { return _has_preloaded_classes; }
  static void      set_has_preloaded_classes() { _has_preloaded_classes = true; }
};

#endif // SHARE_CDS_CDSCONFIG_HPP
