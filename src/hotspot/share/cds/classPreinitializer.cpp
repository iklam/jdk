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
#include "cds/archiveBuilder.hpp"
#include "cds/archiveHeapLoader.hpp"
#include "cds/classPreinitializer.hpp"
#include "cds/classPrelinker.hpp"
#include "classfile/classPrinter.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmClasses.hpp"
#include "interpreter/bytecodeStream.hpp"
#include "interpreter/bytecodeTracer.hpp"
#include "memory/iterator.hpp"
#include "memory/resourceArea.hpp"
#include "oops/constantPool.inline.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "runtime/handles.inline.hpp"

GrowableArrayCHeap<InstanceKlass*, mtClassShared>* ClassPreinitializer::_dumptime_classes = nullptr;
Array<InstanceKlass*>* ClassPreinitializer::_runtime_classes = nullptr;
ClassPreinitializer::ClassesTable* ClassPreinitializer::_is_preinit_safe = nullptr;

void ClassPreinitializer::initialize(TRAPS) {
  if (DumpSharedSpaces) {
    _dumptime_classes = new GrowableArrayCHeap<InstanceKlass*, mtClassShared>();
    _is_preinit_safe = new (mtClass)ClassesTable();

    ClassLoaderData* cld = ClassLoaderData::the_null_class_loader_data();
    for (Klass* k = cld->klasses(); k != nullptr; k = k->next_link()) {
      if (k->is_instance_klass()) {
        InstanceKlass* ik = InstanceKlass::cast(k);
        if (ik->is_initialized()) {
          // These classes are required to execute the very early stage of VM
          // start-up, and their <clinit> contains code that cannot be skipped
          // (native calls, etc).
          _is_preinit_safe->put(ik, PreInitType::EARLY);
          ResourceMark rm;
          log_debug(cds, heap, init)("vm early init %s", ik->external_name());
        }
      }
    }
  } else if (ArchiveHeapLoader::is_in_use()) {
    if (_runtime_classes != nullptr) {
      for (int i = 0; i < _runtime_classes->length(); i++) {
        _runtime_classes->at(i)->update_preinited_class(CHECK);
      }
    }
  }
}

void ClassPreinitializer::setup_preinit_classes(TRAPS) {
  if (!DumpSharedSpaces) {
    return;
  }

  ClassLoaderData* cld = ClassLoaderData::the_null_class_loader_data();
  for (Klass* k = cld->klasses(); k != nullptr; k = k->next_link()) {
    if (k->is_instance_klass()) {
      InstanceKlass* ik = InstanceKlass::cast(k);
      if (ClassPrelinker::is_vm_class(ik)) {
        check_preinit_safety(ik);
      }
    }
  }

  for (int i = 0; i < _dumptime_classes->length(); i++) {
    InstanceKlass* ik = _dumptime_classes->at(i);
    if (!ik->is_initialized()) {
      assert(ik->class_initializer() == nullptr, "<clinit> not supported yet");
      ResourceMark rm;
      log_debug(cds, heap, init)("force init of safe class %s", ik->external_name());
      ik->initialize(CHECK);
    }
  }
}

bool ClassPreinitializer::is_safe_class(InstanceKlass* ik) {
  PreInitType* v = _is_preinit_safe->get(ik);
  return (v != nullptr && *v != PreInitType::UNSAFE);
}

bool ClassPreinitializer::check_preinit_safety(InstanceKlass* ik) {
  // We can handle classes that are known to be loaded when
  // ClassPreinitializer::initialize() is called at runtime.
  assert(ClassPrelinker::is_vm_class(ik), "must be");

  PreInitType* v = _is_preinit_safe->get(ik);
  if (v != nullptr) { // already checked.
    return *v != PreInitType::UNSAFE;
  } else {
    bool is_safe = check_preinit_safety_impl(ik);
    _is_preinit_safe->put(ik, is_safe ? PreInitType::SAFE : PreInitType::UNSAFE);
    if (is_safe) {
      ResourceMark rm;
      oop mirror = ik->java_mirror();
      log_info(cds, heap, init)("safe %s (mirror = %d bytes)", ik->external_name(), int(mirror->size()) * BytesPerWord);
      _dumptime_classes->append(ik);
    }
    return is_safe;
  }
}

bool ClassPreinitializer::check_preinit_safety_impl(InstanceKlass* ik) {
  assert(ik->java_super() != nullptr, "java/lang/Object should already be in _is_preinit_safe table");

  if (ik->name() == vmSymbols::jdk_internal_misc_UnsafeConstants()) {
      ResourceMark rm;
      log_debug(cds, heap, init)("unsafe %s, static fields are initialized by HotSpot",
                                 ik->external_name());
      return false;
  }

  if (!check_preinit_safety(ik->java_super())) {
    ResourceMark rm;
    log_debug(cds, heap, init)("unsafe %s, super is not safe %s", ik->external_name(), ik->java_super()->external_name());
    return false;
  }

  auto check_interfs = [&] (InstanceKlass* interface) {
    if (!check_preinit_safety(interface)) {
      ResourceMark rm;
      log_debug(cds, heap, init)("unsafe %s, interface is not safe %s",
                                 ik->external_name(), interface->external_name());
      return false;
    } else {
      return true;
    }
  };

  if (!ik->iterate_local_interfaces(check_interfs)) {
    return false;
  }

  if (ik->class_initializer() != nullptr) {
    SafeMethodChecker checker(ik, ik->class_initializer());
    if (!checker.check_safety(nullptr)) {
      ResourceMark rm;
      log_debug(cds, heap, init)("unsafe %s, has unsafe <clinit>", ik->external_name());
      return false;
    }
  }

  for (JavaFieldStream fs(ik); !fs.done(); fs.next()) {
    if (fs.access_flags().is_static()) {
      fieldDescriptor& fd = fs.field_descriptor();
      if (!fs.access_flags().is_final()) {
        ResourceMark rm;
        log_debug(cds, heap, init)("unsafe %s, has non-final static field %s:%s",
                                   ik->external_name(), fd.name()->as_C_string(), fd.signature()->as_C_string());
        return false;
      }
    }
  }

  return true;
}

void ClassPreinitializer::copy_mirror_if_safe(Klass* k, oop scratch_mirror) {
  if (!k->is_instance_klass()) {
    return;
  }
  InstanceKlass* ik = InstanceKlass::cast(k);
  PreInitType* v = _is_preinit_safe->get(ik);
  if (v == nullptr || *v != PreInitType::SAFE) {
    return;
  }

  oop orig_mirror = k->java_mirror();
  ResourceMark rm;
  log_debug(cds, heap, init)("Copying initialized mirror for %s", ik->external_name());

  for (JavaFieldStream fs(ik); !fs.done(); fs.next()) {
    if (fs.access_flags().is_static()) {
      fieldDescriptor& fd = fs.field_descriptor();
      BasicType field_type = fd.field_type();
      assert(fs.access_flags().is_final(), "Must be");
      switch (field_type) {
      case T_OBJECT: {
          oop value = orig_mirror->obj_field(fd.offset());
          if (fs.initval_index() != 0) {
            assert(java_lang_String::is_instance(value), "must be (JVM spec)");
          }
          scratch_mirror->obj_field_put(fd.offset(), value);
        }
        break;
      case T_BOOLEAN: {
          jboolean value = orig_mirror->bool_field(fd.offset());
          scratch_mirror->bool_field_put(fd.offset(), value);
        }
        break;
      case T_BYTE: {
          jbyte value = orig_mirror->byte_field(fd.offset());
          scratch_mirror->byte_field_put(fd.offset(), value);
        }
        break;
      case T_SHORT: {
          jshort value = orig_mirror->short_field(fd.offset());
          scratch_mirror->short_field_put(fd.offset(), value);
        }
        break;
      case T_CHAR: {
          jchar value = orig_mirror->char_field(fd.offset());
          scratch_mirror->char_field_put(fd.offset(), value);
        }
        break;
      case T_INT: {
          jint value = orig_mirror->int_field(fd.offset());
          scratch_mirror->int_field_put(fd.offset(), value);
        }
        break;
      case T_LONG: {
          jlong value = orig_mirror->long_field(fd.offset());
          scratch_mirror->long_field_put(fd.offset(), value);
        }
        break;
      case T_FLOAT: {
          jfloat value = orig_mirror->float_field(fd.offset());
          scratch_mirror->float_field_put(fd.offset(), value);
        }
        break;
      case T_DOUBLE: {
          jdouble value = orig_mirror->double_field(fd.offset());
          scratch_mirror->double_field_put(fd.offset(), value);
        }
        break;
      default:
        Unimplemented();
        ShouldNotReachHere();
      }
    }
  }
}

void ClassPreinitializer::write_tables() {
  int num = _dumptime_classes->length();
  _runtime_classes = ArchiveBuilder::new_ro_array<InstanceKlass*>(num);
  for (int i = 0; i < num; i++) {
    _runtime_classes->at_put(i, ArchiveBuilder::get_buffered_klass(_dumptime_classes->at(i)));
    ArchivePtrMarker::mark_pointer(_runtime_classes->adr_at(i));
  }
  log_info(cds)("%d classes will be pre-initialized at VM start-up", num);
}

void ClassPreinitializer::serialize_tables(SerializeClosure* soc) {
  soc->do_ptr((void**)&_runtime_classes);
}

SafeMethodChecker::SafeMethodChecker(InstanceKlass* ik, Method* method) 
  : _init_klass(ik), _method(method), _bc(nullptr) {
  assert(ik->is_linked(), "bytecodes must have been rewritten");
  _locals = NEW_C_HEAP_ARRAY(Value, _method->max_locals(), mtClassShared);
  _stack = new Stack();
  _failed = false;
}

bool SafeMethodChecker::check_safety(SafeMethodChecker::Stack* caller_stack) {
  for (int i = _method->size_of_parameters() - 1; i >= 0; i--) {
    _locals[i] = caller_stack->pop();
  }

  log_debug(cds, heap, init)("==================== Checking %s", _method->external_name());

  LogTarget(Trace, cds, heap, init) lt;
  methodHandle mh(Thread::current(), _method);
  BytecodeStream s(mh);
  while (true) {
    _code = s.next();
    _raw_code = s.raw_code();
    address bcp = s.bcp();
    _bci = s.bci();
    _next_bci = s.next_bci();
    _is_wide = (_code == Bytecodes::_wide);
    if (is_wide()) {
      _code = Bytecodes::code_at(_method, bcp+1);
    }
    assert(_code >= 0, "We must have ended on a return instruction"); // we don't handles loops or exceptions yet

    if (lt.is_enabled()) {
      ResourceMark rm;
      stringStream ss;
      int flags = ClassPrinter::PRINT_METHOD_NAME |
                  ClassPrinter::PRINT_BYTECODE |
                  ClassPrinter::PRINT_DYNAMIC |
                  ClassPrinter::PRINT_METHOD_HANDLE;
      BytecodeTracer::print_method_codes(mh, _bci, _next_bci, &ss, flags);
      char* s = ss.as_string();
      size_t len = strlen(s);
      if (len > 0 && s[len-1] == '\n') {
        s[len-1] = 0;
      }
      lt.print("[%2d] %s", _stack->length(), s);
    }

    Bytecode bc = s.bytecode();
    _bc = &bc;

    switch (_code) {
    case Bytecodes::_ldc:
    case Bytecodes::_ldc_w:
    case Bytecodes::_ldc2_w:
      load_constant();
      break;

    case Bytecodes::_new:
      new_instance();
      break;

    case Bytecodes::_putstatic:
      put_static();
      break;

    case Bytecodes::_invokestatic:
      simple_invoke(true);
      break;

    case Bytecodes::_invokespecial:
      simple_invoke(false);
      break;

    case Bytecodes::_dup:
      push(_stack->top());
      break;

    case Bytecodes::_iconst_0:
    case Bytecodes::_iconst_1:
    case Bytecodes::_iconst_2:
    case Bytecodes::_iconst_3:
    case Bytecodes::_iconst_4:
    case Bytecodes::_iconst_5:
      push(Value(T_INT)); // FIXME: remember specific const value so we can eliminate branches.
      break;

    case Bytecodes::_return:
      return true; // We haven't found any bad instructions, so we are OK.

    default:
      fail("Unsupported bytecode: %s", Bytecodes::name(_code));
      break;
    }

    if (_failed) {
      return false;
    }
  }
}

int SafeMethodChecker::cpc_to_cp_index(int cpc_index) {
  ConstantPool* constants = method()->constants();
  int i = cpc_index - ConstantPool::CPCACHE_INDEX_TAG;

  assert(i >= 0 && i < constants->cache()->length(), "must be");
  return constants->cache()->entry_at(i)->constant_pool_index();
}

int SafeMethodChecker::object_to_cp_index(int obj_index) {
  ConstantPool* constants = method()->constants();
  int i = obj_index - ConstantPool::CPCACHE_INDEX_TAG;

  assert(i >= 0 && i < constants->resolved_references()->length(), "must be");
  return constants->object_to_cp_index(i);
}

InstanceKlass* SafeMethodChecker::resolve_klass(Symbol* name) {
  if (name == init_klass()->name()) {
    // FIXME this is not right. Need to resolve name from the context of this->method()
    return init_klass();
  } else {
    return nullptr; // FIXME ....
  }
}

Method* SafeMethodChecker::resolve_method(Symbol* klass_name, Symbol* method_name, Symbol* signature, bool is_static) {
  InstanceKlass* ik = resolve_klass(klass_name);
  if (ik == nullptr) {
    return nullptr;
  }
  Method* m = ik->find_method(method_name, signature);
  if (m != nullptr && m->is_static() == is_static) {
    return m;
  } else {
    return nullptr;
  }
}

void SafeMethodChecker::load_constant() {
  int cp_index;
  if (code() == Bytecodes::_ldc) {
    if (Bytecodes::uses_cp_cache(raw_code())) {
      cp_index = object_to_cp_index(get_index_u1_cpcache());
    } else {
      cp_index = get_index_u1();
    }
  } else { // ldc_w, ldc2_w
    if (Bytecodes::uses_cp_cache(raw_code())) {
      cp_index = object_to_cp_index(get_index_u2_cpcache());
    } else {
      cp_index = get_index_u2();
    }
  }

  ConstantPool* constants = method()->constants();
  constantTag tag = constants->tag_at(cp_index);

  if (tag.is_int()) {
    push(Value(T_INT));
  } else if (tag.is_long()) {
    push(Value(T_LONG)); // Huh? Is this OK?
    push(Value(T_LONG));
  } else if (tag.is_float()) {
    push(Value(T_FLOAT));
  } else if (tag.is_double()) {
    push(Value(T_DOUBLE)); // Huh? Is this OK?
    push(Value(T_DOUBLE));
  } else if (tag.is_string()) {
    push(Value(T_OBJECT)); // mark this as a safe object?
  } else if (tag.is_klass() || tag.is_unresolved_klass()) {
    // FIXME -- OK to push current class, or if check_preinit_safety(klass) returns OK
    fail("ldc Class not supported");
  } else if (tag.is_method_type()) {
    fail("ldc MethodType not supported");
  } else if (tag.is_method_handle()) {
    fail("ldc MethodHandle not supported");
  }
}

void SafeMethodChecker::new_instance() {
  int i = get_index_u2();
  Symbol* name = method()->constants()->klass_name_at(i);
  InstanceKlass* k = resolve_klass(name);
  if (k == nullptr || k != init_klass()) {
    // FIXME: now we allow only new'ing of the class being initialized
    fail("Cannot new %s", name->as_C_string());
  }
  push(Value(T_OBJECT)); // TODO: remember type
}

void SafeMethodChecker::simple_invoke(bool is_static) {
  assert(Bytecodes::uses_cp_cache(raw_code()), "must be");
  ConstantPool* constants = method()->constants();
  int i = cpc_to_cp_index(get_index_u2_cpcache());
  assert(constants->tag_at(i).value() == JVM_CONSTANT_Methodref, "must be");

  Symbol* klass_name = constants->klass_name_at(constants->uncached_klass_ref_index_at(i));
  Symbol* method_name = constants->uncached_name_ref_at(i);
  Symbol* signature = constants->uncached_signature_ref_at(i);

  // Some built-in methods ....
  if (is_static &&
      klass_name->equals("java/lang/Class") && 
      method_name->equals("getPrimitiveClass") &&
      signature->equals("(Ljava/lang/String;)Ljava/lang/Class;")) {
    pop();
    push(Value(T_OBJECT));
    return;
  }

  if (!is_static &&
      klass_name->equals("java/lang/Object") && 
      method_name->equals("<init>") &&
      signature->equals("()V")) {
    pop();
    return;
  }

  Method* m = resolve_method(klass_name, method_name, signature, is_static);
  if (m != nullptr && !m->is_native()
      && klass_name->equals("java/lang/Boolean") && 
      method_name->equals("<init>") &&
      signature->equals("(Z)V")) {
    pop();
    pop();
  } else {
    ResourceMark rm;
    fail("Cannot handle %s method %s.%s:%s", is_static ? "static" : "instance",
         klass_name->as_C_string(),
         method_name->as_C_string(), signature->as_C_string());
  }
}

void SafeMethodChecker::put_static() {
  assert(Bytecodes::uses_cp_cache(raw_code()), "must be");
  ConstantPool* constants = method()->constants();
  int i = cpc_to_cp_index(get_index_u2_cpcache());
  assert(constants->tag_at(i).value() == JVM_CONSTANT_Fieldref, "must be");

  Symbol* klass_name = constants->klass_name_at(constants->uncached_klass_ref_index_at(i));
  Symbol* field_name = constants->uncached_name_ref_at(i);
  Symbol* signature = constants->uncached_signature_ref_at(i);
  fieldDescriptor fd;

  if (klass_name == init_klass()->name() &&
      init_klass()->find_local_field(field_name, signature, &fd) &&
      fd.is_static() && fd.is_final()) {
    // FIXME -- check that tos is a safe value
    pop();
    if (is_double_word_type(fd.field_type())) { // FIXME -- use macro.
      pop();
    }
  } else {
    ResourceMark rm;
    fail("Cannot handle put static field %s.%s:%s", klass_name->as_C_string(),
         field_name->as_C_string(), signature->as_C_string());
  }
}

void SafeMethodChecker::fail(const char* format, ...) {
  _failed = true;
  LogMessage(cds, heap, init) msg;
  if (msg.is_debug()) {
    va_list ap;
    va_start(ap, format);
    msg.debug("Failed at bci %i %s", _bci, Bytecodes::name(_code));
    msg.vwrite(LogLevel::Debug, format, ap);
    va_end(ap);
  }
}
