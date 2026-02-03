/*
 * Copyright (c) 2020, 2026, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_AOTCOMPRESSEDPOINTERS_HPP
#define SHARE_CDS_AOTCOMPRESSEDPOINTERS_HPP

#include "memory/allStatic.hpp"
#include "utilities/align.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"

class AOTCompressedPointers: public AllStatic {
  // The base address used for encoding a narrowPtr
  static address _encode_base;

public:
  // For space saving, we can encode the location of metadata objects in the "rw" and "ro"
  // regions using a 32-bit offset from the bottom of the "rw" region. Since the metadata
  // objects are 8-byte aligned, we can encode with a 3-bit shift on 64-bit platforms
  // to accommodate a maximum of 32GB of metadata objects. There's no need for shifts on
  // 32-bit builds as the size of the AOT cache is limited.
  typedef u4 narrowPtr;
  static constexpr int MetadataOffsetShift = LP64_ONLY(3) NOT_LP64(0);
  static constexpr uintx MaxMetadataOffsetBytes = LP64_ONLY(0x100000000ULL << MetadataOffsetShift) NOT_LP64(0x7FFFFFFF);

  static narrowPtr encode_offset(uintx offset_bytes) {
    guarantee(is_aligned(offset_bytes, (size_t)1 << MetadataOffsetShift),
              "offset not aligned for scaled encoding");
    uintx offset_units = offset_bytes >> MetadataOffsetShift;
    return checked_cast<narrowPtr>(offset_units);
  }


  template <typename T>
  static narrowPtr encode(T ptr) {
    address p = static_cast<address>(ptr);
    precond(p >= _encode_base);
    uintx offset_bytes = p - _encode_base;
    return encode_offset;
  }
};

#endif // SHARE_CDS_AOTCOMPRESSEDPOINTERS_HPP
