/*
 * Copyright (c) 1999, 2022, Oracle and/or its affiliates. All rights reserved.
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

#ifndef OS_LINUX_OS_LINUX_HPP
#define OS_LINUX_OS_LINUX_HPP

// Linux_OS defines the interface to Linux operating systems

// Information about the protection of the page at address '0' on this os.
bool zero_page_read_protected() { return true; }

namespace Linux {
  // FIXME -- write accessor classes for these
  //friend class ::CgroupSubsystem;
  //friend class ::OSContainer;
  //friend class ::TestReserveMemorySpecial;

  extern int (*_pthread_getcpuclockid)(pthread_t, clockid_t *);
  extern int (*_pthread_setname_np)(pthread_t, const char*);

  extern address   _initial_thread_stack_bottom;
  extern uintptr_t _initial_thread_stack_size;

  extern const char *_libc_version;
  extern const char *_libpthread_version;

  extern bool _supports_fast_thread_cpu_time;

  extern GrowableArray<int>* _cpu_to_node;
  extern GrowableArray<int>* _nindex_to_node;

  extern size_t _default_large_page_size;

 namespace Internal {
  extern julong _physical_memory;
  extern pthread_t _main_thread;
  extern int _page_size;

  julong available_memory();
  julong physical_memory() { return _physical_memory; }
  void set_physical_memory(julong phys_mem) { _physical_memory = phys_mem; }
  int active_processor_count();

  void initialize_system_info();

  int commit_memory_impl(char* addr, size_t bytes, bool exec);
  int commit_memory_impl(char* addr, size_t bytes,
                                size_t alignment_hint, bool exec);

  void set_libc_version(const char *s)       { _libc_version = s; }
  void set_libpthread_version(const char *s) { _libpthread_version = s; }

  void rebuild_cpu_to_node_map();
  void rebuild_nindex_to_node_map();
  GrowableArray<int>* cpu_to_node()    { return _cpu_to_node; }
  GrowableArray<int>* nindex_to_node()  { return _nindex_to_node; }

  size_t default_large_page_size();
  size_t scan_default_large_page_size();
  os::PageSizes scan_multiple_page_support();

  bool setup_large_page_type(size_t page_size);
  bool transparent_huge_pages_sanity_check(bool warn, size_t pages_size);
  bool hugetlbfs_sanity_check(bool warn, size_t page_size);
  bool shm_hugetlbfs_sanity_check(bool warn, size_t page_size);

  int hugetlbfs_page_size_flag(size_t page_size);

  char* reserve_memory_special_shm(size_t bytes, size_t alignment, char* req_addr, bool exec);
  char* reserve_memory_special_huge_tlbfs(size_t bytes, size_t alignment, size_t page_size, char* req_addr, bool exec);
  bool commit_memory_special(size_t bytes, size_t page_size, char* req_addr, bool exec);

  bool release_memory_special_impl(char* base, size_t bytes);
  bool release_memory_special_shm(char* base, size_t bytes);
  bool release_memory_special_huge_tlbfs(char* base, size_t bytes);

  void print_process_memory_info(outputStream* st);
  void print_system_memory_info(outputStream* st);
  bool print_container_info(outputStream* st);
  void print_steal_info(outputStream* st);
  void print_distro_info(outputStream* st);
  void print_libversion_info(outputStream* st);
  void print_proc_sys_info(outputStream* st);
  bool print_ld_preload_file(outputStream* st);
  void print_uptime_info(outputStream* st);
 }

  struct CPUPerfTicks {
    uint64_t used;
    uint64_t usedKernel;
    uint64_t total;
    uint64_t steal;
    bool     has_steal_ticks;
  };

  // which_logical_cpu=-1 returns accumulated ticks for all cpus.
  bool get_tick_information(CPUPerfTicks* pticks, int which_logical_cpu);
  extern bool _stack_is_executable;
  void *dlopen_helper(const char *name, char *ebuf, int ebuflen);
  void *dll_load_in_vmthread(const char *name, char *ebuf, int ebuflen);
  const char *dll_path(void* lib);

  void init_thread_fpu_state();
  int  get_fpu_control_word();
  void set_fpu_control_word(int fpu_control);
  pthread_t main_thread(void)                                { return os::Linux::Internal::_main_thread; }
  // returns kernel thread id (similar to LWP id on Solaris), which can be
  // used to access /proc
  pid_t gettid();

  address   initial_thread_stack_bottom(void)                { return _initial_thread_stack_bottom; }
  uintptr_t initial_thread_stack_size(void)                  { return _initial_thread_stack_size; }

  int page_size(void)                                        { return os::Linux::Internal::_page_size; }
  void set_page_size(int val)                                { os::Linux::Internal::_page_size = val; }

  intptr_t* ucontext_get_sp(const ucontext_t* uc);
  intptr_t* ucontext_get_fp(const ucontext_t* uc);

  // GNU libc and libpthread version strings
  const char *libc_version()           { return _libc_version; }
  const char *libpthread_version()     { return _libpthread_version; }

  void libpthread_init();
  void sched_getcpu_init();
  bool libnuma_init();
  void* libnuma_dlsym(void* handle, const char* name);
  // libnuma v2 (libnuma_1.2) symbols
  void* libnuma_v2_dlsym(void* handle, const char* name);

  // Return default guard size for the specified thread type
  size_t default_guard_size(os::ThreadType thr_type);

  void capture_initial_stack(size_t max_size);

  // Stack overflow handling
  bool manually_expand_stack(JavaThread * t, address addr);

  // fast POSIX clocks support
  void fast_thread_clock_init(void);

  int pthread_getcpuclockid(pthread_t tid, clockid_t *clock_id) {
    return _pthread_getcpuclockid ? _pthread_getcpuclockid(tid, clock_id) : -1;
  }

  bool supports_fast_thread_cpu_time() {
    return _supports_fast_thread_cpu_time;
  }

  jlong fast_thread_cpu_time(clockid_t clockid);

  // Determine if the vmid is the parent pid for a child in a PID namespace.
  // Return the namespace pid if so, otherwise -1.
  int get_namespace_pid(int vmid);

  // Output structure for query_process_memory_info()
  struct meminfo_t {
    ssize_t vmsize;     // current virtual size
    ssize_t vmpeak;     // peak virtual size
    ssize_t vmrss;      // current resident set size
    ssize_t vmhwm;      // peak resident set size
    ssize_t vmswap;     // swapped out
    ssize_t rssanon;    // resident set size (anonymous mappings, needs 4.5)
    ssize_t rssfile;    // resident set size (file mappings, needs 4.5)
    ssize_t rssshmem;   // resident set size (shared mappings, needs 4.5)
  };

  // Attempts to query memory information about the current process and return it in the output structure.
  // May fail (returns false) or succeed (returns true) but not all output fields are available; unavailable
  // fields will contain -1.
  bool query_process_memory_info(meminfo_t* info);

  // Stack repair handling

   void expand_stack_to(address bottom);

  // none present
 namespace Internal {
  void numa_init();

  typedef int (*sched_getcpu_func_t)(void);
  typedef int (*numa_node_to_cpus_func_t)(int node, unsigned long *buffer, int bufferlen);
  typedef int (*numa_node_to_cpus_v2_func_t)(int node, void *mask);
  typedef int (*numa_max_node_func_t)(void);
  typedef int (*numa_num_configured_nodes_func_t)(void);
  typedef int (*numa_available_func_t)(void);
  typedef int (*numa_tonode_memory_func_t)(void *start, size_t size, int node);
  typedef void (*numa_interleave_memory_func_t)(void *start, size_t size, unsigned long *nodemask);
  typedef void (*numa_interleave_memory_v2_func_t)(void *start, size_t size, struct bitmask* mask);
  typedef struct bitmask* (*numa_get_membind_func_t)(void);
  typedef struct bitmask* (*numa_get_interleave_mask_func_t)(void);
  typedef long (*numa_move_pages_func_t)(int pid, unsigned long count, void **pages, const int *nodes, int *status, int flags);
  typedef void (*numa_set_preferred_func_t)(int node);
  typedef void (*numa_set_bind_policy_func_t)(int policy);
  typedef int (*numa_bitmask_isbitset_func_t)(struct bitmask *bmp, unsigned int n);
  typedef int (*numa_distance_func_t)(int node1, int node2);

  sched_getcpu_func_t _sched_getcpu;
  numa_node_to_cpus_func_t _numa_node_to_cpus;
  numa_node_to_cpus_v2_func_t _numa_node_to_cpus_v2;
  numa_max_node_func_t _numa_max_node;
  numa_num_configured_nodes_func_t _numa_num_configured_nodes;
  numa_available_func_t _numa_available;
  numa_tonode_memory_func_t _numa_tonode_memory;
  numa_interleave_memory_func_t _numa_interleave_memory;
  numa_interleave_memory_v2_func_t _numa_interleave_memory_v2;
  numa_set_bind_policy_func_t _numa_set_bind_policy;
  numa_bitmask_isbitset_func_t _numa_bitmask_isbitset;
  numa_distance_func_t _numa_distance;
  numa_get_membind_func_t _numa_get_membind;
  numa_get_interleave_mask_func_t _numa_get_interleave_mask;
  numa_move_pages_func_t _numa_move_pages;
  numa_set_preferred_func_t _numa_set_preferred;
  unsigned long* _numa_all_nodes;
  struct bitmask* _numa_all_nodes_ptr;
  struct bitmask* _numa_nodes_ptr;
  struct bitmask* _numa_interleave_bitmask;
  struct bitmask* _numa_membind_bitmask;

  void set_sched_getcpu(sched_getcpu_func_t func) { _sched_getcpu = func; }
  void set_numa_node_to_cpus(numa_node_to_cpus_func_t func) { _numa_node_to_cpus = func; }
  void set_numa_node_to_cpus_v2(numa_node_to_cpus_v2_func_t func) { _numa_node_to_cpus_v2 = func; }
  void set_numa_max_node(numa_max_node_func_t func) { _numa_max_node = func; }
  void set_numa_num_configured_nodes(numa_num_configured_nodes_func_t func) { _numa_num_configured_nodes = func; }
  void set_numa_available(numa_available_func_t func) { _numa_available = func; }
  void set_numa_tonode_memory(numa_tonode_memory_func_t func) { _numa_tonode_memory = func; }
  void set_numa_interleave_memory(numa_interleave_memory_func_t func) { _numa_interleave_memory = func; }
  void set_numa_interleave_memory_v2(numa_interleave_memory_v2_func_t func) { _numa_interleave_memory_v2 = func; }
  void set_numa_set_bind_policy(numa_set_bind_policy_func_t func) { _numa_set_bind_policy = func; }
  void set_numa_bitmask_isbitset(numa_bitmask_isbitset_func_t func) { _numa_bitmask_isbitset = func; }
  void set_numa_distance(numa_distance_func_t func) { _numa_distance = func; }
  void set_numa_get_membind(numa_get_membind_func_t func) { _numa_get_membind = func; }
  void set_numa_get_interleave_mask(numa_get_interleave_mask_func_t func) { _numa_get_interleave_mask = func; }
  void set_numa_move_pages(numa_move_pages_func_t func) { _numa_move_pages = func; }
  void set_numa_set_preferred(numa_set_preferred_func_t func) { _numa_set_preferred = func; }
  void set_numa_all_nodes(unsigned long* ptr) { _numa_all_nodes = ptr; }
  void set_numa_all_nodes_ptr(struct bitmask **ptr) { _numa_all_nodes_ptr = (ptr == NULL ? NULL : *ptr); }
  void set_numa_nodes_ptr(struct bitmask **ptr) { _numa_nodes_ptr = (ptr == NULL ? NULL : *ptr); }
  void set_numa_interleave_bitmask(struct bitmask* ptr)     { _numa_interleave_bitmask = ptr ;   }
  void set_numa_membind_bitmask(struct bitmask* ptr)        { _numa_membind_bitmask = ptr ;      }
  int sched_getcpu_syscall(void);
 }

  enum NumaAllocationPolicy{
    NotInitialized,
    Membind,
    Interleave
  };
 namespace Internal {
   extern NumaAllocationPolicy _current_numa_policy;
 }

#ifdef __GLIBC__
  struct glibc_mallinfo {
    int arena;
    int ordblks;
    int smblks;
    int hblks;
    int hblkhd;
    int usmblks;
    int fsmblks;
    int uordblks;
    int fordblks;
    int keepcost;
  };

  struct glibc_mallinfo2 {
    size_t arena;
    size_t ordblks;
    size_t smblks;
    size_t hblks;
    size_t hblkhd;
    size_t usmblks;
    size_t fsmblks;
    size_t uordblks;
    size_t fordblks;
    size_t keepcost;
  };

  typedef struct glibc_mallinfo (*mallinfo_func_t)(void);
  typedef struct glibc_mallinfo2 (*mallinfo2_func_t)(void);

 namespace Internal {
  mallinfo_func_t _mallinfo;
  mallinfo2_func_t _mallinfo2;
 }

  int sched_getcpu()  { return Internal::_sched_getcpu != NULL ? Internal::_sched_getcpu() : -1; }
  int numa_node_to_cpus(int node, unsigned long *buffer, int bufferlen);
  int numa_max_node() { return Internal::_numa_max_node != NULL ? Internal::_numa_max_node() : -1; }
  int numa_num_configured_nodes() {
    return Internal::_numa_num_configured_nodes != NULL ? Internal::_numa_num_configured_nodes() : -1;
  }
  int numa_available() { return Internal::_numa_available != NULL ? Internal::_numa_available() : -1; }
  int numa_tonode_memory(void *start, size_t size, int node) {
    return Internal::_numa_tonode_memory != NULL ? Internal::_numa_tonode_memory(start, size, node) : -1;
  }

  bool is_running_in_interleave_mode() {
    return Internal::_current_numa_policy == Interleave;
  }

  void set_configured_numa_policy(NumaAllocationPolicy numa_policy) {
    Internal::_current_numa_policy = numa_policy;
  }

  NumaAllocationPolicy identify_numa_policy() {
    for (int node = 0; node <= Linux::numa_max_node(); node++) {
      if (Internal::_numa_bitmask_isbitset(Internal::_numa_interleave_bitmask, node)) {
        return Interleave;
      }
    }
    return Membind;
  }

  void numa_interleave_memory(void *start, size_t size) {
    // Prefer v2 API
    if (Internal::_numa_interleave_memory_v2 != NULL) {
      if (is_running_in_interleave_mode()) {
        Internal::_numa_interleave_memory_v2(start, size, Internal::_numa_interleave_bitmask);
      } else if (Internal::_numa_membind_bitmask != NULL) {
        Internal::_numa_interleave_memory_v2(start, size, Internal::_numa_membind_bitmask);
      }
    } else if (Internal::_numa_interleave_memory != NULL) {
      Internal::_numa_interleave_memory(start, size, Internal::_numa_all_nodes);
    }
  }
  void numa_set_preferred(int node) {
    if (Internal::_numa_set_preferred != NULL) {
      Internal::_numa_set_preferred(node);
    }
  }
  void numa_set_bind_policy(int policy) {
    if (Internal::_numa_set_bind_policy != NULL) {
      Internal::_numa_set_bind_policy(policy);
    }
  }
  int numa_distance(int node1, int node2) {
    return Internal::_numa_distance != NULL ? Internal::_numa_distance(node1, node2) : -1;
  }
  long numa_move_pages(int pid, unsigned long count, void **pages, const int *nodes, int *status, int flags) {
    return Internal::_numa_move_pages != NULL ? Internal::_numa_move_pages(pid, count, pages, nodes, status, flags) : -1;
  }
  int get_node_by_cpu(int cpu_id);
  int get_existing_num_nodes();
  // Check if numa node is configured (non-zero memory node).
  bool is_node_in_configured_nodes(unsigned int n) {
    if (Internal::_numa_bitmask_isbitset != NULL && Internal::_numa_all_nodes_ptr != NULL) {
      return Internal::_numa_bitmask_isbitset(Internal::_numa_all_nodes_ptr, n);
    } else
      return false;
  }
  // Check if numa node exists in the system (including zero memory nodes).
  bool is_node_in_existing_nodes(unsigned int n) {
    if (Internal::_numa_bitmask_isbitset != NULL && Internal::_numa_nodes_ptr != NULL) {
      return Internal::_numa_bitmask_isbitset(Internal::_numa_nodes_ptr, n);
    } else if (Internal::_numa_bitmask_isbitset != NULL && Internal::_numa_all_nodes_ptr != NULL) {
      // Not all libnuma API v2 implement numa_nodes_ptr, so it's not possible
      // to trust the API version for checking its absence. On the other hand,
      // numa_nodes_ptr found in libnuma 2.0.9 and above is the only way to get
      // a complete view of all numa nodes in the system, hence numa_nodes_ptr
      // is used to handle CPU and nodes on architectures (like PowerPC) where
      // there can exist nodes with CPUs but no memory or vice-versa and the
      // nodes may be non-contiguous. For most of the architectures, like
      // x86_64, numa_node_ptr presents the same node set as found in
      // numa_all_nodes_ptr so it's possible to use numa_all_nodes_ptr as a
      // substitute.
      return Internal::_numa_bitmask_isbitset(Internal::_numa_all_nodes_ptr, n);
    } else
      return false;
  }
  // Check if node is in bound node set.
  bool is_node_in_bound_nodes(int node) {
    if (Internal::_numa_bitmask_isbitset != NULL) {
      if (is_running_in_interleave_mode()) {
        return Internal::_numa_bitmask_isbitset(Internal::_numa_interleave_bitmask, node);
      } else {
        return Internal::_numa_membind_bitmask != NULL ? Internal::_numa_bitmask_isbitset(Internal::_numa_membind_bitmask, node) : false;
      }
    }
    return false;
  }
  // Check if bound to only one numa node.
  // Returns true if bound to a single numa node, otherwise returns false.
  bool is_bound_to_single_node() {
    int nodes = 0;
    unsigned int node = 0;
    unsigned int highest_node_number = 0;

    if (Internal::_numa_membind_bitmask != NULL && Internal::_numa_max_node != NULL && Internal::_numa_bitmask_isbitset != NULL) {
      highest_node_number = Internal::_numa_max_node();
    } else {
      return false;
    }

    for (node = 0; node <= highest_node_number; node++) {
      if (Internal::_numa_bitmask_isbitset(Internal::_numa_membind_bitmask, node)) {
        nodes++;
      }
    }

    if (nodes == 1) {
      return true;
    } else {
      return false;
    }
  }

  const GrowableArray<int>* numa_nindex_to_node() {
    return _nindex_to_node;
  }
#endif
};

#endif // OS_LINUX_OS_LINUX_HPP
