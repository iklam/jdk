/*
 * Copyright (c) 2012, 2020, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_UTILITIES_RESOURCEHASH_HPP
#define SHARE_UTILITIES_RESOURCEHASH_HPP

#include "memory/allocation.hpp"

template<
    typename K, typename V,
    unsigned (*HASH)  (K const&),
    bool     (*EQUALS)(K const&, K const&),
    ResourceObj::allocation_type ALLOC_TYPE,
    MEMFLAGS MEM_TYPE
    >
class BaseResourceHashtable : public ResourceObj {
protected:
  class Node : public ResourceObj {
   public:
    unsigned _hash;
    K _key;
    V _value;
    Node* _next;

    Node(unsigned hash, K const& key, V const& value) :
        _hash(hash), _key(key), _value(value), _next(NULL) {}

    // Create a node with a default-constructed value.
    Node(unsigned hash, K const& key) :
        _hash(hash), _key(key), _value(), _next(NULL) {}

  };

  // Returns a pointer to where the node where the value would reside if
  // it's in the table.
  static inline Node** lookup_node(unsigned hash, K const& key, Node** table, int size) {
    unsigned index = hash % size;
    Node** ptr = &table[index];
    while (*ptr != NULL) {
      Node* node = *ptr;
      if (node->_hash == hash && EQUALS(key, node->_key)) {
        break;
      }
      ptr = &(node->_next);
    }
    return ptr;
  }

  static inline void deallocate(Node** table, int size) {
    if (ALLOC_TYPE == C_HEAP) {
      Node* const* bucket = table;
      while (bucket < &table[size]) {
        Node* node = *bucket;
        while (node != NULL) {
          Node* cur = node;
          node = node->_next;
          delete cur;
        }
        ++bucket;
      }
    }
  }

  inline static V* get(K const& key, Node** table, int size) {
    unsigned hv = HASH(key);
    Node** ptr = lookup_node(hv, key, table, size);
    if (*ptr != NULL) {
      return const_cast<V*>(&(*ptr)->_value);
    } else {
      return NULL;
    }
  }

 /**
  * Inserts or replaces a value in the table.
  * @return: true:  if a new item is added
  *          false: if the item already existed and the value is updated
  */
  inline static bool put(K const& key, V const& value, Node** table, int size) {
    unsigned hv = HASH(key);
    Node** ptr = lookup_node(hv, key, table, size);
    if (*ptr != NULL) {
      (*ptr)->_value = value;
      return false;
    } else {
      *ptr = new (ALLOC_TYPE, MEM_TYPE) Node(hv, key, value);
      return true;
    }
  }

  // Look up the key.
  // If an entry for the key exists, leave map unchanged and return a pointer to its value.
  // If no entry for the key exists, create a new entry from key and a default-created value
  //  and return a pointer to the value.
  // *p_created is true if entry was created, false if entry pre-existed.
  inline static V* put_if_absent(K const& key, bool* p_created, Node** table, int size) {
    unsigned hv = HASH(key);
    Node** ptr = lookup_node(hv, key, table, size);
    if (*ptr == NULL) {
      *ptr = new (ALLOC_TYPE, MEM_TYPE) Node(hv, key);
      *p_created = true;
    } else {
      *p_created = false;
    }
    return &(*ptr)->_value;
  }

  // Look up the key.
  // If an entry for the key exists, leave map unchanged and return a pointer to its value.
  // If no entry for the key exists, create a new entry from key and value and return a
  //  pointer to the value.
  // *p_created is true if entry was created, false if entry pre-existed.
  inline static V* put_if_absent(K const& key, V const& value, bool* p_created, Node** table, int size) {
    unsigned hv = HASH(key);
    Node** ptr = lookup_node(hv, key, table, size);
    if (*ptr == NULL) {
      *ptr = new (ALLOC_TYPE, MEM_TYPE) Node(hv, key, value);
      *p_created = true;
    } else {
      *p_created = false;
    }
    return &(*ptr)->_value;
  }

  bool remove(K const& key, Node** table, int size) {
    unsigned hv = HASH(key);
    Node** ptr = lookup_node(hv, key, table, size);

    Node* node = *ptr;
    if (node != NULL) {
      *ptr = node->_next;
      if (ALLOC_TYPE == C_HEAP) {
        delete node;
      }
      return true;
    }
    return false;
  }

  // ITER contains bool do_entry(K const&, V const&), which will be
  // called for each entry in the table.  If do_entry() returns false,
  // the iteration is cancelled.
  template<class ITER>
  static void iterate(ITER* iter, Node* const* table, int size) {
    Node* const* bucket = table;
    while (bucket < &table[size]) {
      Node* node = *bucket;
      while (node != NULL) {
        bool cont = iter->do_entry(node->_key, node->_value);
        if (!cont) { return; }
        node = node->_next;
      }
      ++bucket;
    }
  }
};

// This is the old ResourceHashtable where the SIZE is a built-time constant

template<
    typename K, typename V,
    // xlC does not compile this:
    // http://stackoverflow.com/questions/8532961/template-argument-of-type-that-is-defined-by-inner-typedef-from-other-template-c
    //typename ResourceHashtableFns<K>::hash_fn   HASH   = primitive_hash<K>,
    //typename ResourceHashtableFns<K>::equals_fn EQUALS = primitive_equals<K>,
    unsigned (*HASH)  (K const&)           = primitive_hash<K>,
    bool     (*EQUALS)(K const&, K const&) = primitive_equals<K>,
    unsigned SIZE = 256,
    ResourceObj::allocation_type ALLOC_TYPE = ResourceObj::RESOURCE_AREA,
    MEMFLAGS MEM_TYPE = mtInternal
    >
class ResourceHashtable : public BaseResourceHashtable<K, V, HASH, EQUALS, ALLOC_TYPE, MEM_TYPE> {
 private:
  typedef class BaseResourceHashtable<K, V, HASH, EQUALS, ALLOC_TYPE, MEM_TYPE> MySuper;
  typedef class MySuper::Node Node;

  Node* _table[SIZE];

 public:
  ResourceHashtable() { memset(_table, 0, SIZE * sizeof(Node*)); }

  ~ResourceHashtable() {
    MySuper::deallocate(_table, SIZE);
  }

  bool contains(K const& key) const {
    return get(key) != NULL;
  }

  V* get(K const& key) const {
    return MySuper::get(key, (Node**)_table, SIZE);
  }

  bool put(K const& key, V const& value) {
    return MySuper::put(key, value, _table, SIZE);
  }

  V* put_if_absent(K const& key, bool* p_created) {
    return MySuper::put_if_absent(key, p_created, _table, SIZE);
  }

  V* put_if_absent(K const& key, V const& value, bool* p_created) {
    return MySuper::put_if_absent(key, value, p_created, _table, SIZE);
  }

  bool remove(K const& key) {
    return MySuper::remove(key, _table, SIZE);
  }

  template<class ITER>
  void iterate(ITER* iter) const {
    return MySuper::iterate(iter, _table, SIZE);
  }
};

// This is the new ResourceHashtable where the _size is given at runtime

template<
    typename K, typename V,
    unsigned (*HASH)  (K const&)           = primitive_hash<K>,    // see comments in ResourceHashtable
    bool     (*EQUALS)(K const&, K const&) = primitive_equals<K>,  // see comments in ResourceHashtable
    ResourceObj::allocation_type ALLOC_TYPE = ResourceObj::RESOURCE_AREA,
    MEMFLAGS MEM_TYPE = mtInternal
    >
class ResourceHashtableXX : public BaseResourceHashtable<K, V, HASH, EQUALS, ALLOC_TYPE, MEM_TYPE> {
 private:
  typedef class BaseResourceHashtable<K, V, HASH, EQUALS, ALLOC_TYPE, MEM_TYPE> MySuper;
  typedef class MySuper::Node Node;

  int _size;
  Node** _table;

 public:
  ResourceHashtableXX(int size) : _size(size) {
    if (ALLOC_TYPE == ResourceObj::C_HEAP) {
      _table = NEW_C_HEAP_ARRAY(Node*, _size, MEM_TYPE);
    } else {
      _table = NEW_RESOURCE_ARRAY(Node*, _size);
    }
    memset(_table, 0, _size * sizeof(Node*));
  }

  ~ResourceHashtableXX() {
    MySuper::deallocate(_table, _size);
    if (ALLOC_TYPE == ResourceObj::C_HEAP) {
      FREE_C_HEAP_ARRAY(Node*, _table);
    }
  }

  bool contains(K const& key) const {
    return get(key) != NULL;
  }

  V* get(K const& key) const {
    return MySuper::get(key, (Node**)_table, _size);
  }

  bool put(K const& key, V const& value) {
    return MySuper::put(key, value, _table, _size);
  }

  V* put_if_absent(K const& key, bool* p_created) {
    return MySuper::put_if_absent(key, p_created, _table, _size);
  }

  V* put_if_absent(K const& key, V const& value, bool* p_created) {
    return MySuper::put_if_absent(key, value, p_created, _table, _size);
  }

  bool remove(K const& key) {
    return MySuper::remove(key, _table, _size);
  }

  template<class ITER>
  void iterate(ITER* iter) const {
    return MySuper::iterate(iter, _table, _size);
  }
};

#endif // SHARE_UTILITIES_RESOURCEHASH_HPP
