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

#ifndef SHARE_CDS_CLASSPREINITIALIZER_HPP
#define SHARE_CDS_CLASSPREINITIALIZER_HPP

#include "oops/array.hpp"
#include "oops/constantPool.hpp"
#include "oops/oopsHierarchy.hpp"
#include "interpreter/bytecode.hpp"
#include "interpreter/bytecodes.hpp"
#include "memory/allocation.hpp"
#include "memory/allStatic.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"
#include "utilities/resourceHash.hpp"
#include "utilities/growableArray.hpp"
#include "utilities/align.hpp"
#include "utilities/bytes.hpp"

class BytecodeStream;
class SerializeClosure;

// ClassPreinitializer stores qualified classes into the CDS archive in an
// initialized state.
//
// At run time, such classes are already loaded and fully initialized at VM
// start up.
class ClassPreinitializer : AllStatic {
  enum class PreInitType : int {
    EARLY = 0,
    SAFE = 1,
    UNSAFE = 2
  };
  using ClassesTable = ResourceHashtable<InstanceKlass*, PreInitType, 15889, AnyObj::C_HEAP, mtClassShared>;
  static GrowableArrayCHeap<InstanceKlass*, mtClassShared>* _dumptime_classes;
  static Array<InstanceKlass*>* _runtime_classes;
  static ClassesTable* _is_preinit_safe;

  static bool check_preinit_safety(InstanceKlass* ik);
  static bool check_preinit_safety_impl(InstanceKlass* ik);

public:
  static void initialize(TRAPS);
  static void setup_preinit_classes(TRAPS);
  static void copy_mirror_if_safe(Klass* k, oop scratch_mirror);
  static void write_tables();
  static void serialize_tables(SerializeClosure* soc);
  static bool is_safe_class(InstanceKlass* ik);
};

// Check if a method runs "safe" code only (for some defintions of "safe")
// - it can only store "safe values" into static final fields of _inited_klass
//   - "safe values" to be defined.
// - it can only access certain "safe" methods (to be defined)
// - it can only return "safe values"
//
// Currently this is very basic and very conservative.
class SafeMethodChecker: StackObj {
  class Value {
  public:
    bool _valid;
    BasicType _type;

    Value() : _valid(false), _type(T_ILLEGAL) {}
    Value(BasicType type) : _valid(true), _type(type) {}
    void merge_from(const Value other);
  };

  class Frame { // The values of the local vars and expression stack at a given bci
    using States = GrowableArrayCHeap<Value, mtClassShared>;
    int _max_locals;
    States* _states;
    bool is_stack_empty() { return  _states->length() <= _max_locals; }
    bool is_local(int i) { return 0 <= i && i < _max_locals; }

  public:
    Frame(int max_locals);
    Frame() : _max_locals(-1), _states(nullptr) {}
    ~Frame();

    void push(Value v) { _states->push(v); }
    Value pop() {
      assert(!is_stack_empty(), "unverflow");
      return _states->pop();
    }
    int stack_length() { return _states->length() - _max_locals; }
    Value stack_top() {
      assert(!is_stack_empty(), "sanity");
      return _states->top();
    }

    void local_at_put(int i, Value v) { assert(is_local(i), "must be"); _states->at_put(i, v); }
    Value local_at(int i) { assert(is_local(i), "must be"); return _states->at(i); }

    void merge_from(Frame& frame);
    void clone_to(Frame& frame);
  };

  class BranchTarget {
    bool _is_pending;       // Is the target bci of this branch in _pending_branches
    Frame _frame;          // The frame when we start executing this branch.
  public:
    BranchTarget() : _is_pending(false), _frame() {}
  //BranchTarget(int bci, Frame& frame) : _is_pending(false), _frame(frame) {}
    bool is_pending() const { return _is_pending; }
    void set_is_pending(bool v) { _is_pending = v; }
    void merge_from(Frame& other) { _frame.merge_from(other); }
    void clone_to(Frame& other) { _frame.clone_to(other); }
  };

  static int _nest_level;
  InstanceKlass* _init_klass;   // The class is being analyzed.
  Method* _method;              // Is this method "safe" when it's executed during
                                // the initialization of _init_klass?
  Frame _current_frame;         // frame state at _bci
  bool _failed;
  int _bci;
  int _next_bci;

  bool      _is_wide;
  Bytecode* _bc;                // FIXME -- why can't this be 'Bytecode _bc'?
  BytecodeStream* _bytecode_stream;
  Bytecodes::Code _code;
  Bytecodes::Code _raw_code;
  bool _ended;

  using BranchTargetsTable = ResourceHashtable<int, BranchTarget, 347, AnyObj::C_HEAP, mtClassShared>;
  BranchTargetsTable* _branch_targets;
  GrowableArrayCHeap<int, mtClassShared>* _pending_branches;

  InstanceKlass* init_klass() const { return _init_klass; }
  Method*   method()          const { return _method; }
  bool      is_wide()         const { return _is_wide; }

  int get_index_u1_cpcache()  const { return _bc->get_index_u1_cpcache(raw_code());  }
  int get_index_u2_cpcache()  const { return _bc->get_index_u2_cpcache(raw_code());  }
  int get_index_u1()          const { return _bc->get_index_u1(raw_code()); }
  int get_index_u2()          const { return _bc->get_index_u2(raw_code()); }

  Bytecodes::Code raw_code()  const { return _raw_code; }
  Bytecodes::Code code()      const { return _code; }

  // For ldc bytecodes
  int object_to_cp_index(int obj_index);

  // For invoke, field, etc
  int cpc_to_cp_index(int cpc_index);

  InstanceKlass* resolve_klass(Symbol* name); // as resolved by this->method()
  Method* resolve_method(Symbol* klass_name, Symbol* method_name, Symbol* signature, bool is_static);

  void load_constant();
  void put_static();
  void new_instance();
  void simple_invoke(bool is_static); // Hmmm, how to invoke interface, or virtual??
  bool invoke_method(Method* m);
  void if_branch(int dest_bci);
  void goto_branch(int dest_bci);

  void maybe_branch_to(int dest_bci);
  void end_basic_block();

  void push(Value v) { _current_frame.push(v); }
  Value pop()        { return _current_frame.pop(); }

  void local_at_put(int i, Value v) { _current_frame.local_at_put(i, v); }
  Value local_at(int i) { return _current_frame.local_at(i); }

  static const char* indent();
  void fail(const char* format, ...) ATTRIBUTE_PRINTF(2, 3);
public:
  SafeMethodChecker(InstanceKlass* init_klass, Method* method);
  ~SafeMethodChecker();

  // Perform abstract execution on the method's bytecode. Incoming parameters
  // are popped from the caller's stack. Return value, if any, is pushed onto
  // the caller's stack.
  bool check_safety();
};


#endif // SHARE_CDS_CLASSPREINITIALIZER_HPP
