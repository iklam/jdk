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

#ifndef SHARE_GC_GCALLFLAGS_HPP
#define SHARE_GC_GCALLFLAGS_HPP

#include "gc/shared/gc_globals.hpp"
#include "gc/shared/tlab_globals.hpp"
#include "utilities/macros.hpp"

#if INCLUDE_G1GC
#include "gc/g1/g1_globals.hpp"
#endif
#if INCLUDE_ZGC
#include "gc/z/z_globals.hpp"
#endif

#define GC_ALL_FLAGS(         \
    develop,                  \
    develop_pd,               \
    product,                  \
    product_pd,               \
    notproduct,               \
    range,                    \
    constraint)               \
                              \
  GC_FLAGS(                   \
    develop,                  \
    develop_pd,               \
    product,                  \
    product_pd,               \
    notproduct,               \
    range,                    \
    constraint)               \
                              \
  TLAB_FLAGS(                 \
    develop,                  \
    develop_pd,               \
    product,                  \
    product_pd,               \
    notproduct,               \
    range,                    \
    constraint)               \
                              \
  G1GC_ONLY(GC_G1_FLAGS(      \
    develop,                  \
    develop_pd,               \
    product,                  \
    product_pd,               \
    notproduct,               \
    range,                    \
    constraint))              \
                              \
  ZGC_ONLY(GC_Z_FLAGS(        \
    develop,                  \
    develop_pd,               \
    product,                  \
    product_pd,               \
    notproduct,               \
    range,                    \
    constraint))

#endif // SHARE_GC_GCALLFLAGS_HPP
