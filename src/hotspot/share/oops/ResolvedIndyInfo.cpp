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

#include "precompiled.hpp"
#include "code/compressedStream.hpp"
#include "oops/method.hpp"
#include "oops/ResolvedIndyInfo.hpp"

bool ResolvedIndyInfo::check_no_old_or_obsolete_entry() {
    // return false if m refers to a non-deleted old or obsolete method
    if (_method != nullptr) {
        assert(_method->is_valid() && _method->is_method(), "m is a valid method");
        return !_method->is_old() && !_method->is_obsolete(); // old is always set for old and obsolete
    } else {
        return true;
    }
}

// ResolvedInvokeDynamicInfo
void ResolvedIndyInfo::print_on(outputStream* st) const {
    st->print_cr("Resolved InvokeDynamic Info:");
    st->print_cr(" - Method: " INTPTR_FORMAT " %s", p2i(method()), method()->external_name());
    st->print_cr(" - Resolved References Index: %d", resolved_references_index());
    st->print_cr(" - CP Index: %d", cpool_index());
    st->print_cr(" - Num Parameters: %d", num_parameters());
    st->print_cr(" - Return type: %s", type2name(as_BasicType((TosState)return_type())));
    st->print_cr(" - Has Appendix: %d", has_appendix());
}

void ResolvedIndyInfo::metaspace_pointers_do(MetaspaceClosure* it) {
    it->push(&_method);
}
