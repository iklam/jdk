/*
 * Copyright (c) 2021, 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_LAMBDAPROXYCLASSINFO_HPP
#define SHARE_CDS_LAMBDAPROXYCLASSINFO_HPP
#include "cds/metaspaceShared.hpp"
#include "classfile/javaClasses.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/resourceHash.hpp"

class InstanceKlass;
class Method;
class Symbol;
class outputStream;

class LambdaProxyClassKey {
  InstanceKlass* _caller_ik;
  Symbol*        _invoked_name;
  Symbol*        _invoked_type;
  Symbol*        _method_type;
  Method*        _member_method;
  Symbol*        _instantiated_method_type;

public:
  LambdaProxyClassKey(InstanceKlass* caller_ik,
                      Symbol*        invoked_name,
                      Symbol*        invoked_type,
                      Symbol*        method_type,
                      Method*        member_method,
                      Symbol*        instantiated_method_type) :
    _caller_ik(caller_ik),
    _invoked_name(invoked_name),
    _invoked_type(invoked_type),
    _method_type(method_type),
    _member_method(member_method),
    _instantiated_method_type(instantiated_method_type) {}

  bool equals(LambdaProxyClassKey const& other) const {
    return _caller_ik == other._caller_ik &&
           _invoked_name == other._invoked_name &&
           _invoked_type == other._invoked_type &&
           _method_type == other._method_type &&
           _member_method == other._member_method &&
           _instantiated_method_type == other._instantiated_method_type;
  }

  unsigned int hash() const;

  static unsigned int dumptime_hash(Symbol* sym)  {
    if (sym == nullptr) {
      // _invoked_name maybe null
      return 0;
    }
    return java_lang_String::hash_code((const jbyte*)sym->bytes(), sym->utf8_length());
  }

  unsigned int dumptime_hash() const {
    return dumptime_hash(_caller_ik->name()) +
           dumptime_hash(_invoked_name) +
           dumptime_hash(_invoked_type) +
           dumptime_hash(_method_type) +
           dumptime_hash(_instantiated_method_type);
  }

  static inline unsigned int DUMPTIME_HASH(LambdaProxyClassKey const& key) {
    return (key.dumptime_hash());
  }

  static inline bool DUMPTIME_EQUALS(
      LambdaProxyClassKey const& k1, LambdaProxyClassKey const& k2) {
    return (k1.equals(k2));
  }

  InstanceKlass* caller_ik()         const { return _caller_ik; }
  Symbol* invoked_name()             const { return _invoked_name; }
  Symbol* invoked_type()             const { return _invoked_type; }
  Symbol* method_type()              const { return _method_type; }
  Method* member_method()            const { return _member_method; }
  Symbol* instantiated_method_type() const { return _instantiated_method_type; }

#ifndef PRODUCT
  void print_on(outputStream* st) const;
#endif
};

class RunTimeLambdaProxyClassKey {
  u4 _caller_ik;
  u4 _invoked_name;
  u4 _invoked_type;
  u4 _method_type;
  Method* _member_method;
  u4 _instantiated_method_type;

public:
  RunTimeLambdaProxyClassKey(LambdaProxyClassKey& key) {
    if (ArchiveBuilder::is_active()) {
      ArchiveBuilder* b = ArchiveBuilder::current();
      _caller_ik                = b->any_to_offset_u4(key.caller_ik());
      _invoked_name             = b->any_to_offset_u4(key.invoked_name());
      _invoked_type             = b->any_to_offset_u4(key.invoked_type());
      _method_type              = b->any_to_offset_u4(key.method_type());
      _instantiated_method_type = b->any_to_offset_u4(key.instantiated_method_type());
    } else {
      _caller_ik                = ArchiveBuilder::to_offset_u4(uintx(key.caller_ik()) - uintx(SharedBaseAddress));
      _invoked_name             = ArchiveBuilder::to_offset_u4(uintx(key.invoked_name()) - uintx(SharedBaseAddress));
      _invoked_type             = ArchiveBuilder::to_offset_u4(uintx(key.invoked_type()) - uintx(SharedBaseAddress));
      _method_type              = ArchiveBuilder::to_offset_u4(uintx(key.method_type()) - uintx(SharedBaseAddress));
      _instantiated_method_type = ArchiveBuilder::to_offset_u4(uintx(key.instantiated_method_type()) - uintx(SharedBaseAddress));
    }

    _member_method = key.member_method();
  }

  unsigned int hash() const;
  bool equals(RunTimeLambdaProxyClassKey const& other) const {
    return _caller_ik == other._caller_ik &&
           _invoked_name == other._invoked_name &&
           _invoked_type == other._invoked_type &&
           _method_type == other._method_type &&
           _member_method == other._member_method &&
           _instantiated_method_type == other._instantiated_method_type;
  }

  void remove_unshareable_info() {
    _member_method = nullptr;
  }

#ifndef PRODUCT
  void print_on(outputStream* st) const;
#endif
};

class DumpTimeLambdaProxyClassInfo {
public:
  GrowableArray<InstanceKlass*>* _proxy_klasses;
  DumpTimeLambdaProxyClassInfo() : _proxy_klasses(nullptr) {}
  DumpTimeLambdaProxyClassInfo& operator=(const DumpTimeLambdaProxyClassInfo&) = delete;
  ~DumpTimeLambdaProxyClassInfo();

  void add_proxy_klass(InstanceKlass* proxy_klass) {
    if (_proxy_klasses == nullptr) {
      _proxy_klasses = new (mtClassShared) GrowableArray<InstanceKlass*>(5, mtClassShared);
    }
    assert(_proxy_klasses != nullptr, "sanity");
    _proxy_klasses->append(proxy_klass);
  }

  void metaspace_pointers_do(MetaspaceClosure* it) {
    for (int i=0; i<_proxy_klasses->length(); i++) {
      it->push(_proxy_klasses->adr_at(i));
    }
  }
};

class RunTimeLambdaProxyClassInfo {
  RunTimeLambdaProxyClassKey _key;
  InstanceKlass* _proxy_klass_head;
public:
  RunTimeLambdaProxyClassInfo(RunTimeLambdaProxyClassKey key, InstanceKlass* proxy_klass_head) :
    _key(key), _proxy_klass_head(proxy_klass_head) {}

  InstanceKlass* proxy_klass_head() const { return _proxy_klass_head; }

  // Used by LambdaProxyClassDictionary to implement OffsetCompactHashtable::EQUALS
  static inline bool EQUALS(
       const RunTimeLambdaProxyClassInfo* value, RunTimeLambdaProxyClassKey* key, int len_unused) {
    return (value->_key.equals(*key));
  }
  void init(LambdaProxyClassKey& key, DumpTimeLambdaProxyClassInfo& info);

  unsigned int hash() const {
    return _key.hash();
  }
  RunTimeLambdaProxyClassKey key() const {
    return _key;
  }
#ifndef PRODUCT
  void print_on(outputStream* st) const;
#endif
};

class DumpTimeLambdaProxyClassDictionary
  : public ResourceHashtable<LambdaProxyClassKey,
                             DumpTimeLambdaProxyClassInfo,
                             137, // prime number
                             AnyObj::C_HEAP,
                             mtClassShared,
                             LambdaProxyClassKey::DUMPTIME_HASH,
                             LambdaProxyClassKey::DUMPTIME_EQUALS> {
public:
  DumpTimeLambdaProxyClassDictionary() : _count(0) {}
  int _count;
};

class LambdaProxyClassDictionary : public OffsetCompactHashtable<
  RunTimeLambdaProxyClassKey*,
  const RunTimeLambdaProxyClassInfo*,
  RunTimeLambdaProxyClassInfo::EQUALS> {};

#endif // SHARE_CDS_LAMBDAPROXYCLASSINFO_HPP
