/*
 * Copyright (c) 2021, Oracle and/or its affiliates. All rights reserved.
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
#include "cds/classListWriter.hpp"
#include "classfile/classLoader.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/moduleEntry.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "memory/resourceArea.hpp"
#include "oops/instanceKlass.hpp"
#include "runtime/mutexLocker.hpp"

fileStream* ClassListWriter::_classlist_file = NULL;

void ClassListWriter::init() {
  // For -XX:DumpLoadedClassList=<file> option
  if (DumpLoadedClassList != NULL) {
    const char* list_name = make_log_name(DumpLoadedClassList, NULL);
    _classlist_file = new(ResourceObj::C_HEAP, mtInternal)
                         fileStream(list_name);
    _classlist_file->print_cr("# NOTE: Do not modify this file.");
    _classlist_file->print_cr("#");
    _classlist_file->print_cr("# This file is generated via the -XX:DumpLoadedClassList=<class_list_file> option");
    _classlist_file->print_cr("# and is used at CDS archive dump time (see -Xshare:dump).");
    _classlist_file->print_cr("#");
    FREE_C_HEAP_ARRAY(char, list_name);
  }
}

void ClassListWriter::write(const InstanceKlass* k) {
  if (!is_enabled()) {
    return;
  }

  if (!ClassLoader::has_jrt_entry()) {
    warning("DumpLoadedClassList and CDS are not supported in exploded build");
    DumpLoadedClassList = NULL;
    return;
  }

  ClassListWriter w;
  write_to_stream(k, w.stream());
}

void ClassListWriter::write_to_stream(const InstanceKlass* k, outputStream* stream) {
  ClassLoaderData* loader_data = k->class_loader_data();
  if (!SystemDictionaryShared::is_sharing_possible(loader_data)) {
    return;
  }

  if (k->is_hidden()) {
    return;
  }

  if (k->module()->is_patched()) {
    return;
  }

  ResourceMark rm;
  stream->print_cr("%s", k->name()->as_C_string());
  stream->flush();
}

void ClassListWriter::delete_classlist() {
  if (_classlist_file != NULL) {
    delete _classlist_file;
  }
}
