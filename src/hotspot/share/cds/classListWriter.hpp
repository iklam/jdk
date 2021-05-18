/*
 * Copyright (c) 2020, 2021, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_CLASSLISTWRITER_HPP
#define SHARE_CDS_CLASSLISTWRITER_HPP

#include "runtime/mutexLocker.hpp"
#include "runtime/thread.hpp"
#include "utilities/ostream.hpp"

class ClassListWriter {
#if INCLUDE_CDS
  static fileStream* _classlist_file;
  MutexLocker _locker;
public:
  ClassListWriter() : _locker(Thread::current(), ClassListFile_lock, Mutex::_no_safepoint_check_flag) {}

  outputStream* stream() {
    return _classlist_file;
  }

  static bool is_enabled() {
    return _classlist_file != NULL && _classlist_file->is_open();
  }

#else
public:
  static bool is_enabled() {
    return false;
  }
#endif


  static void init() NOT_CDS_RETURN;
  static void write(const InstanceKlass* k) NOT_CDS_RETURN;
  static void write_to_stream(const InstanceKlass* k, outputStream* stream) NOT_CDS_RETURN;
  static void delete_classlist() NOT_CDS_RETURN;
};

#endif // SHARE_CDS_CLASSLISTWRITER_HPP
