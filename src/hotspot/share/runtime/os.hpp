/*
 * Copyright (c) 1997, 2022, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_RUNTIME_OS_HPP
#define SHARE_RUNTIME_OS_HPP

#include "jvm_md.h"
#include "metaprogramming/integralConstant.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/ostream.hpp"
#include "utilities/macros.hpp"
#ifndef _WINDOWS
# include <setjmp.h>
#endif
#ifdef __APPLE__
# include <mach/mach_time.h>
#endif

class AgentLibrary;
class frame;

// os defines the interface to operating system; this includes traditional
// OS services (time, I/O) as well as other functionality with system-
// dependent code.

class CgroupSubsystem;
class Thread;
class JavaThread;
class NativeCallStack;
class methodHandle;
class OSContainer;
class OSThread;
class Mutex;
class TestReserveMemorySpecial;

struct hostent;
struct jvmtiTimerInfo;

template<class E> class GrowableArray;

// %%%%% Moved ThreadState, START_FN, OSThread to new osThread.hpp. -- Rose

// Platform-independent error return values from OS functions
enum OSReturn {
  OS_OK         =  0,        // Operation was successful
  OS_ERR        = -1,        // Operation failed
  OS_INTRPT     = -2,        // Operation was interrupted
  OS_TIMEOUT    = -3,        // Operation timed out
  OS_NOMEM      = -5,        // Operation failed for lack of memory
  OS_NORESOURCE = -6         // Operation failed for lack of nonmemory resource
};

enum ThreadPriority {        // JLS 20.20.1-3
  NoPriority       = -1,     // Initial non-priority value
  MinPriority      =  1,     // Minimum priority
  NormPriority     =  5,     // Normal (non-daemon) priority
  NearMaxPriority  =  9,     // High priority, used for VMThread
  MaxPriority      = 10,     // Highest priority, used for WatcherThread
                             // ensures that VMThread doesn't starve profiler
  CriticalPriority = 11      // Critical thread priority
};

enum WXMode {
  WXWrite,
  WXExec
};

// Executable parameter flag for os::commit_memory() and
// os::commit_memory_or_exit().
const bool ExecMem = true;

// Typedef for structured exception handling support
typedef void (*java_call_t)(JavaValue* value, const methodHandle& method, JavaCallArguments* args, JavaThread* thread);

class MallocTracker;

namespace os {
  namespace Internal {}
  //using namespace Internal;

  //friend class VMStructs;
  //friend class JVMCIVMStructs;
  //friend class MallocTracker;

#ifdef ASSERT
  namespace Internal {
    extern bool _mutex_init_done;
  }
  void set_mutex_init_done() { Internal::_mutex_init_done = true; }
  bool mutex_init_done() { return Internal::_mutex_init_done; }
#endif

  // A simple value class holding a set of page sizes (similar to sigset_t)
  class PageSizes {
    size_t _v; // actually a bitmap.
  public:
    PageSizes() : _v(0) {}
    void add(size_t pagesize);
    bool contains(size_t pagesize) const;
    // Given a page size, return the next smaller page size in this set, or 0.
    size_t next_smaller(size_t pagesize) const;
    // Given a page size, return the next larger page size in this set, or 0.
    size_t next_larger(size_t pagesize) const;
    // Returns the largest page size in this set, or 0 if set is empty.
    size_t largest() const;
    // Returns the smallest page size in this set, or 0 if set is empty.
    size_t smallest() const;
    // Prints one line of comma separated, human readable page sizes, "empty" if empty.
    void print_on(outputStream* st) const;
  };

 namespace Internal {
  extern OSThread*          _starting_thread;
  extern address            _polling_page;
  extern PageSizes          _page_sizes;

  char*  pd_reserve_memory(size_t bytes, bool executable);

  char*  pd_attempt_reserve_memory_at(char* addr, size_t bytes, bool executable);

  bool   pd_commit_memory(char* addr, size_t bytes, bool executable);
  bool   pd_commit_memory(char* addr, size_t size, size_t alignment_hint,
                                 bool executable);
  // Same as pd_commit_memory() that either succeeds or calls
  // vm_exit_out_of_memory() with the specified mesg.
  void   pd_commit_memory_or_exit(char* addr, size_t bytes,
                                         bool executable, const char* mesg);
  void   pd_commit_memory_or_exit(char* addr, size_t size,
                                         size_t alignment_hint,
                                         bool executable, const char* mesg);
  bool   pd_uncommit_memory(char* addr, size_t bytes, bool executable);
  bool   pd_release_memory(char* addr, size_t bytes);

  char*  pd_attempt_map_memory_to_file_at(char* addr, size_t bytes, int file_desc);

  char*  pd_map_memory(int fd, const char* file_name, size_t file_offset,
                           char *addr, size_t bytes, bool read_only = false,
                           bool allow_exec = false);
  char*  pd_remap_memory(int fd, const char* file_name, size_t file_offset,
                             char *addr, size_t bytes, bool read_only,
                             bool allow_exec);
  bool   pd_unmap_memory(char *addr, size_t bytes);
  void   pd_free_memory(char *addr, size_t bytes, size_t alignment_hint);
  void   pd_realign_memory(char *addr, size_t bytes, size_t alignment_hint);

  char*  pd_reserve_memory_special(size_t size, size_t alignment, size_t page_size,

                                          char* addr, bool executable);
  bool   pd_release_memory_special(char* addr, size_t bytes);

  size_t page_size_for_region(size_t region_size, size_t min_pages, bool must_be_aligned);

  // Get summary strings for system information in buffer provided
  void  get_summary_cpu_info(char* buf, size_t buflen);
  void  get_summary_os_info(char* buf, size_t buflen);

  void initialize_initial_active_processor_count();

  LINUX_ONLY(void pd_init_container_support();)

  extern volatile unsigned int _rand_seed;    // seed for random number generator
  extern int _processor_count;                // number of processors
  extern int _initial_active_processor_count; // number of active processors during initialization.
 }

  void init(void);                      // Called before command line parsing

  void init_container_support() {       // Called during command line parsing.
     LINUX_ONLY(os::Internal::pd_init_container_support();)
  }

  void init_before_ergo(void);          // Called after command line parsing
                                               // before VM ergonomics processing.
  jint init_2(void);                    // Called after command line parsing
                                               // and VM ergonomics processing

  // Get environ pointer, platform independently
  char** get_environ();

  bool have_special_privileges();

  jlong  javaTimeMillis();
  jlong  javaTimeNanos();
  void   javaTimeNanos_info(jvmtiTimerInfo *info_ptr);
  void   javaTimeSystemUTC(jlong &seconds, jlong &nanos);
  void   run_periodic_checks();

  // Returns the elapsed time in seconds since the vm started.
  double elapsedTime();

  // Returns real time in seconds since an arbitrary point
  // in the past.
  bool getTimesSecs(double* process_real_time,
                           double* process_user_time,
                           double* process_system_time);

  // Interface to the performance counter
  jlong elapsed_counter();
  jlong elapsed_frequency();

  // The "virtual time" of a thread is the amount of time a thread has
  // actually run.  The first function indicates whether the OS supports
  // this functionality for the current thread, and if so the second
  // returns the elapsed virtual time for the current thread.
  bool supports_vtime();
  double elapsedVTime();

  // Return current local time in a string (YYYY-MM-DD HH:MM:SS).
  // It is MT safe, but not async-safe, as reading time zone
  // information may require a lock on some platforms.
  char*      local_time_string(char *buf, size_t buflen);
  struct tm* localtime_pd     (const time_t* clock, struct tm*  res);
  struct tm* gmtime_pd        (const time_t* clock, struct tm*  res);

  // "YYYY-MM-DDThh:mm:ss.mmm+zzzz" incl. terminating zero
  const size_t iso8601_timestamp_size = 29;

  // Fill in buffer with an ISO-8601 string corresponding to the given javaTimeMillis value
  // E.g., YYYY-MM-DDThh:mm:ss.mmm+zzzz.
  // Returns buffer, or NULL if it failed.
  char* iso8601_time(jlong milliseconds_since_19700101, char* buffer,
                            size_t buffer_length, bool utc = false);

  // Fill in buffer with current local time as an ISO-8601 string.
  // E.g., YYYY-MM-DDThh:mm:ss.mmm+zzzz.
  // Returns buffer, or NULL if it failed.
  char* iso8601_time(char* buffer, size_t buffer_length, bool utc = false);

  // Interface for detecting multiprocessor system
  inline bool is_MP() {
    // During bootstrap if _processor_count is not yet initialized
    // we claim to be MP as that is safest. If any platform has a
    // stub generator that might be triggered in this phase and for
    // which being declared MP when in fact not, is a problem - then
    // the bootstrap routine for the stub generator needs to check
    // the processor count directly and leave the bootstrap routine
    // in place until called after initialization has occurred.
    return (Internal::_processor_count != 1);
  }

  julong available_memory();
  julong physical_memory();
  bool has_allocatable_memory_limit(size_t* limit);
  bool is_server_class_machine();

  // Returns the id of the processor on which the calling thread is currently executing.
  // The returned value is guaranteed to be between 0 and (os::processor_count() - 1).
  uint processor_id();

  // number of CPUs
  int processor_count() {
    return Internal::_processor_count;
  }
  void set_processor_count(int count) { Internal::_processor_count = count; }

  // Returns the number of CPUs this process is currently allowed to run on.
  // Note that on some OSes this can change dynamically.
  int active_processor_count();

  // At startup the number of active CPUs this process is allowed to run on.
  // This value does not change dynamically. May be different from active_processor_count().
  int initial_active_processor_count() {
    assert(Internal::_initial_active_processor_count > 0, "Initial active processor count not set yet.");
    return Internal::_initial_active_processor_count;
  }

  // Give a name to the current thread.
  void set_native_thread_name(const char *name);

  // Interface for stack banging (predetect possible stack overflow for
  // exception processing)  There are guard pages, and above that shadow
  // pages for stack overflow checking.
  bool uses_stack_guard_pages();
  bool must_commit_stack_guard_pages();
  void map_stack_shadow_pages(address sp);
  bool stack_shadow_pages_available(Thread *thread, const methodHandle& method, address sp);

 namespace Internal {
  // Minimum stack size a thread can be created with (allowing
  // the VM to completely create the thread and enter user code).
  // The initial values exclude any guard pages (by HotSpot or libc).
  // set_minimum_stack_sizes() will add the size required for
  // HotSpot guard pages depending on page size and flag settings.
  // Libc guard pages are never considered by these values.
  extern size_t _compiler_thread_min_stack_allowed;
  extern size_t _java_thread_min_stack_allowed;
  extern size_t _vm_internal_thread_min_stack_allowed;
  extern size_t _os_min_stack_allowed;

  // Check and sets minimum stack sizes
  jint set_minimum_stack_sizes();
 }

  // Find committed memory region within specified range (start, start + size),
  // return true if found any
  bool committed_in_range(address start, size_t size, address& committed_start, size_t& committed_size);

  // OS interface to Virtual Memory

  // Return the default page size.
  int    vm_page_size();

  // The set of page sizes which the VM is allowed to use (may be a subset of
  //  the page sizes actually available on the platform).
  const PageSizes& page_sizes() { return Internal::_page_sizes; }

  // Returns the page size to use for a region of memory.
  // region_size / min_pages will always be greater than or equal to the
  // returned value. The returned value will divide region_size.
  size_t page_size_for_region_aligned(size_t region_size, size_t min_pages);

  // Returns the page size to use for a region of memory.
  // region_size / min_pages will always be greater than or equal to the
  // returned value. The returned value might not divide region_size.
  size_t page_size_for_region_unaligned(size_t region_size, size_t min_pages);

  // Return the largest page size that can be used
  size_t max_page_size() { return page_sizes().largest(); }

  // Return a lower bound for page sizes. Also works before os::init completed.
  size_t min_page_size() { return 4 * K; }

  // Methods for tracing page sizes returned by the above method.
  // The region_{min,max}_size parameters should be the values
  // passed to page_size_for_region() and page_size should be the result of that
  // call.  The (optional) base and size parameters should come from the
  // ReservedSpace base() and size() methods.
  void trace_page_sizes(const char* str, const size_t* page_sizes, int count);
  void trace_page_sizes(const char* str,
                               const size_t region_min_size,
                               const size_t region_max_size,
                               const size_t page_size,
                               const char* base,
                               const size_t size);
  void trace_page_sizes_for_requested_size(const char* str,
                                                  const size_t requested_size,
                                                  const size_t page_size,
                                                  const size_t alignment,
                                                  const char* base,
                                                  const size_t size);

  int    vm_allocation_granularity();

  // Reserves virtual memory.
  char*  reserve_memory(size_t bytes, bool executable = false, MEMFLAGS flags = mtOther);

  // Reserves virtual memory that starts at an address that is aligned to 'alignment'.
  char*  reserve_memory_aligned(size_t size, size_t alignment, bool executable = false);

  // Attempts to reserve the virtual memory at [addr, addr + bytes).
  // Does not overwrite existing mappings.
  char*  attempt_reserve_memory_at(char* addr, size_t bytes, bool executable = false);

  bool   commit_memory(char* addr, size_t bytes, bool executable);
  bool   commit_memory(char* addr, size_t size, size_t alignment_hint,
                              bool executable);
  // Same as commit_memory() that either succeeds or calls
  // vm_exit_out_of_memory() with the specified mesg.
  void   commit_memory_or_exit(char* addr, size_t bytes,
                                      bool executable, const char* mesg);
  void   commit_memory_or_exit(char* addr, size_t size,
                                      size_t alignment_hint,
                                      bool executable, const char* mesg);
  bool   uncommit_memory(char* addr, size_t bytes, bool executable = false);
  bool   release_memory(char* addr, size_t bytes);

  // A diagnostic function to print memory mappings in the given range.
  void print_memory_mappings(char* addr, size_t bytes, outputStream* st);
  // Prints all mappings
  void print_memory_mappings(outputStream* st);

  // Touch memory pages that cover the memory range from start to end
  // (exclusive) to make the OS back the memory range with actual memory.
  // Other threads may use the memory range concurrently with pretouch.
  void   pretouch_memory(void* start, void* end, size_t page_size = vm_page_size());

  enum ProtType { MEM_PROT_NONE, MEM_PROT_READ, MEM_PROT_RW, MEM_PROT_RWX };
  bool   protect_memory(char* addr, size_t bytes, ProtType prot,
                               bool is_committed = true);

  bool   guard_memory(char* addr, size_t bytes);
  bool   unguard_memory(char* addr, size_t bytes);
  bool   create_stack_guard_pages(char* addr, size_t bytes);
  bool   pd_create_stack_guard_pages(char* addr, size_t bytes);
  bool   remove_stack_guard_pages(char* addr, size_t bytes);
  // Helper function to create a new file with template jvmheap.XXXXXX.
  // Returns a valid fd on success or else returns -1
  int create_file_for_heap(const char* dir);
  // Map memory to the file referred by fd. This function is slightly different from map_memory()
  // and is added to be used for implementation of -XX:AllocateHeapAt
  char* map_memory_to_file(size_t size, int fd);
  char* map_memory_to_file_aligned(size_t size, size_t alignment, int fd);
  char* map_memory_to_file(char* base, size_t size, int fd);
  char* attempt_map_memory_to_file_at(char* base, size_t size, int fd);
  // Replace existing reserved memory with file mapping
  char* replace_existing_mapping_with_file_mapping(char* base, size_t size, int fd);

  char*  map_memory(int fd, const char* file_name, size_t file_offset,
                           char *addr, size_t bytes, bool read_only = false,
                           bool allow_exec = false, MEMFLAGS flags = mtNone);
  char*  remap_memory(int fd, const char* file_name, size_t file_offset,
                             char *addr, size_t bytes, bool read_only,
                             bool allow_exec);
  bool   unmap_memory(char *addr, size_t bytes);
  void   free_memory(char *addr, size_t bytes, size_t alignment_hint);
  void   realign_memory(char *addr, size_t bytes, size_t alignment_hint);

  // NUMA-specific interface
  bool   numa_has_static_binding();
  bool   numa_has_group_homing();
  void   numa_make_local(char *addr, size_t bytes, int lgrp_hint);
  void   numa_make_global(char *addr, size_t bytes);
  size_t numa_get_groups_num();
  size_t numa_get_leaf_groups(int *ids, size_t size);
  bool   numa_topology_changed();
  int    numa_get_group_id();
  int    numa_get_group_id_for_address(const void* address);

  // Page manipulation
  struct page_info {
    size_t size;
    int lgrp_id;
  };
  bool   get_page_info(char *start, page_info* info);
  char*  scan_pages(char *start, char* end, page_info* page_expected, page_info* page_found);

  char*  non_memory_address_word();
  // reserve, commit and pin the entire memory region
  char*  reserve_memory_special(size_t size, size_t alignment, size_t page_size,
                                       char* addr, bool executable);
  bool   release_memory_special(char* addr, size_t bytes);
  void   large_page_init();
  size_t large_page_size();
  bool   can_commit_large_page_memory();
  bool   can_execute_large_page_memory();

  // Check if pointer points to readable memory (by 4-byte read access)
  bool    is_readable_pointer(const void* p);
  bool    is_readable_range(const void* from, const void* to);

  // threads

  enum ThreadType {
    vm_thread,
    gc_thread,         // GC thread
    java_thread,       // Java, CodeCacheSweeper, JVMTIAgent and Service threads.
    compiler_thread,
    watcher_thread,
    asynclog_thread,   // dedicated to flushing logs
    os_thread
  };

  bool create_thread(Thread* thread,
                            ThreadType thr_type,
                            size_t req_stack_size = 0);

  // The "main thread", also known as "starting thread", is the thread
  // that loads/creates the JVM via JNI_CreateJavaVM.
  bool create_main_thread(JavaThread* thread);

  // The primordial thread is the initial process thread. The java
  // launcher never uses the primordial thread as the main thread, but
  // applications that host the JVM directly may do so. Some platforms
  // need special-case handling of the primordial thread if it attaches
  // to the VM.
  bool is_primordial_thread(void)
#if defined(_WINDOWS) || defined(BSD)
    // No way to identify the primordial thread.
    { return false; }
#else
  ;
#endif

  bool create_attached_thread(JavaThread* thread);
  void pd_start_thread(Thread* thread);
  void start_thread(Thread* thread);

  // Returns true if successful.
  bool signal_thread(Thread* thread, int sig, const char* reason);

  void free_thread(OSThread* osthread);

  // thread id on Linux/64bit is 64bit, on Windows it's 32bit
  intx current_thread_id();
  int current_process_id();

  // Short standalone OS sleep routines suitable for slow path spin loop.
  // Ignores safepoints/suspension/Thread.interrupt() (so keep it short).
  // ms/ns = 0, will sleep for the least amount of time allowed by the OS.
  // Maximum sleep time is just under 1 second.
  void naked_short_sleep(jlong ms);
  void naked_short_nanosleep(jlong ns);
  // Longer standalone OS sleep routine - a convenience wrapper around
  // multiple calls to naked_short_sleep. Only for use by non-JavaThreads.
  void naked_sleep(jlong millis);
  // Never returns, use with CAUTION
  void infinite_sleep();
  void naked_yield () ;
  OSReturn set_priority(Thread* thread, ThreadPriority priority);
  OSReturn get_priority(const Thread* const thread, ThreadPriority& priority);

  int pd_self_suspend_thread(Thread* thread);

  address    fetch_frame_from_context(const void* ucVoid, intptr_t** sp, intptr_t** fp);
  frame      fetch_frame_from_context(const void* ucVoid);
  frame      fetch_compiled_frame_from_context(const void* ucVoid);

  void breakpoint();
  bool start_debugging(char *buf, int buflen);

  address current_stack_pointer();
  address current_stack_base();
  size_t current_stack_size();

  void verify_stack_alignment() PRODUCT_RETURN;

  bool message_box(const char* title, const char* message);

  // run cmd in a separate process and return its exit code; or -1 on failures.
  // Note: only safe to use in fatal error situations.
  int fork_and_exec(const char *cmd);

  // Call ::exit() on all platforms
  void exit(int num);

  // Call ::_exit() on all platforms. Similar semantics to die() except we never
  // want a core dump.
  void _exit(int num);

  // Terminate the VM, but don't exit the process
  void shutdown();

  // Terminate with an error.  Default is to generate a core file on platforms
  // that support such things.  This calls shutdown() and then aborts.
  void abort(bool dump_core, void *siginfo, const void *context);
  void abort(bool dump_core = true);

  // Die immediately, no exit hook, no abort hook, no cleanup.
  // Dump a core file, if possible, for debugging. os::abort() is the
  // preferred means to abort the VM on error. os::die() should only
  // be called if something has gone badly wrong. CreateCoredumpOnCrash
  // is intentionally not honored by this function.
  void die();

  // File i/o operations
  int open(const char *path, int oflag, int mode);
  FILE* fdopen(int fd, const char* mode);
  FILE* fopen(const char* path, const char* mode);
  jlong lseek(int fd, jlong offset, int whence);
  bool file_exists(const char* file);
  // This function, on Windows, canonicalizes a given path (see os_windows.cpp for details).
  // On Posix, this function is a noop: it does not change anything and just returns
  // the input pointer.
  char* native_path(char *path);
  int ftruncate(int fd, jlong length);
  int get_fileno(FILE* fp);
  void flockfile(FILE* fp);
  void funlockfile(FILE* fp);

  int compare_file_modified_times(const char* file1, const char* file2);

  bool same_files(const char* file1, const char* file2);

  //File i/o operations

  ssize_t read_at(int fd, void *buf, unsigned int nBytes, jlong offset);
  ssize_t write(int fd, const void *buf, unsigned int nBytes);

  // Reading directories.
  DIR*           opendir(const char* dirname);
  struct dirent* readdir(DIR* dirp);
  int            closedir(DIR* dirp);

  // Dynamic library extension
  const char*    dll_file_extension();

  const char*    get_temp_directory();
  const char*    get_current_directory(char *buf, size_t buflen);

  // Builds the platform-specific name of a library.
  // Returns false if the buffer is too small.
  bool           dll_build_name(char* buffer, size_t size,
                                       const char* fname);

  // Builds a platform-specific full library path given an ld path and
  // unadorned library name. Returns true if the buffer contains a full
  // path to an existing file, false otherwise. If pathname is empty,
  // uses the path to the current directory.
  bool           dll_locate_lib(char* buffer, size_t size,
                                       const char* pathname, const char* fname);

  // Symbol lookup, find nearest function name; basically it implements
  // dladdr() for all platforms. Name of the nearest function is copied
  // to buf. Distance from its base address is optionally returned as offset.
  // If function name is not found, buf[0] is set to '\0' and offset is
  // set to -1 (if offset is non-NULL).
  bool dll_address_to_function_name(address addr, char* buf,
                                           int buflen, int* offset,
                                           bool demangle = true);

  // Locate DLL/DSO. On success, full path of the library is copied to
  // buf, and offset is optionally set to be the distance between addr
  // and the library's base address. On failure, buf[0] is set to '\0'
  // and offset is set to -1 (if offset is non-NULL).
  bool dll_address_to_library_name(address addr, char* buf,
                                          int buflen, int* offset);

  // Given an address, attempt to locate both the symbol and the library it
  // resides in. If at least one of these steps was successful, prints information
  // and returns true.
  // - if no scratch buffer is given, stack is used
  // - shorten_paths: path is omitted from library name
  // - demangle: function name is demangled
  // - strip_arguments: arguments are stripped (requires demangle=true)
  // On success prints either one of:
  // "<function name>+<offset> in <library>"
  // "<function name>+<offset>"
  // "<address> in <library>+<offset>"
  bool print_function_and_library_name(outputStream* st,
                                              address addr,
                                              char* buf = NULL, int buflen = 0,
                                              bool shorten_paths = true,
                                              bool demangle = true,
                                              bool strip_arguments = false);

  // Find out whether the pc is in the code for jvm.dll/libjvm.so.
  bool address_is_in_vm(address addr);

  // Loads .dll/.so and
  // in case of error it checks if .dll/.so was built for the
  // same architecture as HotSpot is running on
  // in case of an error NULL is returned and an error message is stored in ebuf
  void* dll_load(const char *name, char *ebuf, int ebuflen);

  // lookup symbol in a shared library
  void* dll_lookup(void* handle, const char* name);

  // Unload library
  void  dll_unload(void *lib);

  // Callback for loaded module information
  // Input parameters:
  //    char*     module_file_name,
  //    address   module_base_addr,
  //    address   module_top_addr,
  //    void*     param
  typedef int (*LoadedModulesCallbackFunc)(const char *, address, address, void *);

  int get_loaded_modules_info(LoadedModulesCallbackFunc callback, void *param);

  // Return the handle of this process
  void* get_default_process_handle();

  // Check for linked agent library
  bool find_builtin_agent(AgentLibrary *agent_lib, const char *syms[],
                                 size_t syms_len);

  // Find agent entry point
  void *find_agent_function(AgentLibrary *agent_lib, bool check_lib,
                                   const char *syms[], size_t syms_len);

  // Provide C99 compliant versions of these functions, since some versions
  // of some platforms don't.
  int vsnprintf(char* buf, size_t len, const char* fmt, va_list args) ATTRIBUTE_PRINTF(3, 0);
  int snprintf(char* buf, size_t len, const char* fmt, ...) ATTRIBUTE_PRINTF(3, 4);

  // Get host name in buffer provided
  bool get_host_name(char* buf, size_t buflen);

  // Print out system information; they are called by fatal error handler.
  // Output format may be different on different platforms.
  void print_os_info(outputStream* st);
  void print_os_info_brief(outputStream* st);
  void print_cpu_info(outputStream* st, char* buf, size_t buflen);
  void pd_print_cpu_info(outputStream* st, char* buf, size_t buflen);
  void print_summary_info(outputStream* st, char* buf, size_t buflen);
  void print_memory_info(outputStream* st);
  void print_dll_info(outputStream* st);
  void print_environment_variables(outputStream* st, const char** env_list);
  void print_context(outputStream* st, const void* context);
  void print_tos_pc(outputStream* st, const void* context);
  void print_register_info(outputStream* st, const void* context);
  bool signal_sent_by_kill(const void* siginfo);
  void print_siginfo(outputStream* st, const void* siginfo);
  void print_signal_handlers(outputStream* st, char* buf, size_t buflen);
  void print_date_and_time(outputStream* st, char* buf, size_t buflen);
  void print_instructions(outputStream* st, address pc, int unitsize);

  // helper for output of seconds in days , hours and months
  void print_dhm(outputStream* st, const char* startStr, long sec);

  void print_location(outputStream* st, intptr_t x, bool verbose = false);
  size_t lasterror(char *buf, size_t len);
  int get_last_error();

  // Replacement for strerror().
  // Will return the english description of the error (e.g. "File not found", as
  //  suggested in the POSIX standard.
  // Will return "Unknown error" for an unknown errno value.
  // Will not attempt to localize the returned string.
  // Will always return a valid string which is a static constant.
  // Will not change the value of errno.
  const char* strerror(int e);

  // Will return the literalized version of the given errno (e.g. "EINVAL"
  //  for EINVAL).
  // Will return "Unknown error" for an unknown errno value.
  // Will always return a valid string which is a static constant.
  // Will not change the value of errno.
  const char* errno_name(int e);

  // wait for a key press if PauseAtExit is set
  void wait_for_keypress_at_exit(void);

  // The following two functions are used by fatal error handler to trace
  // native (C) frames. They are not part of frame.hpp/frame.cpp because
  // frame.hpp/cpp assume thread is JavaThread, and also because different
  // OS/compiler may have different convention or provide different API to
  // walk C frames.
  //
  // We don't attempt to become a debugger, so we only follow frames if that
  // does not require a lookup in the unwind table, which is part of the binary
  // file but may be unsafe to read after a fatal error. So on x86, we can
  // only walk stack if %ebp is used as frame pointer; on ia64, it's not
  // possible to walk C stack without having the unwind table.
  bool is_first_C_frame(frame *fr);
  frame get_sender_for_C_frame(frame *fr);

  // return current frame. pc() and sp() are set to NULL on failure.
  frame      current_frame();

  void print_hex_dump(outputStream* st, address start, address end, int unitsize,
                             int bytes_per_line, address logical_start);
  void print_hex_dump(outputStream* st, address start, address end, int unitsize) {
    print_hex_dump(st, start, end, unitsize, /*bytes_per_line=*/16, /*logical_start=*/start);
  }

  // returns a string to describe the exception/signal;
  // returns NULL if exception_code is not an OS exception/signal.
  const char* exception_name(int exception_code, char* buf, size_t buflen);

  // Returns the signal number (e.g. 11) for a given signal name (SIGSEGV).
  int get_signal_number(const char* signal_name);

  // Returns native Java library, loads if necessary
  void*    native_java_library();

  // Fills in path to jvm.dll/libjvm.so (used by the Disassembler)
  void     jvm_path(char *buf, jint buflen);

  // JNI names
  void     print_jni_name_prefix_on(outputStream* st, int args_size);
  void     print_jni_name_suffix_on(outputStream* st, int args_size);

  // Init os specific system properties values
  void init_system_properties_values();

  // IO operations, non-JVM_ version.
  int stat(const char* path, struct stat* sbuf);
  bool dir_is_empty(const char* path);

  // IO operations on binary files
  int create_binary_file(const char* path, bool rewrite_existing);
  jlong current_file_offset(int fd);
  jlong seek_to_file_offset(int fd, jlong offset);

  // Retrieve native stack frames.
  // Parameter:
  //   stack:  an array to storage stack pointers.
  //   frames: size of above array.
  //   toSkip: number of stack frames to skip at the beginning.
  // Return: number of stack frames captured.
  int get_native_stack(address* stack, int size, int toSkip = 0);

  // General allocation (must be MT-safe)
  void* malloc  (size_t size, MEMFLAGS flags, const NativeCallStack& stack);
  void* malloc  (size_t size, MEMFLAGS flags);
  void* realloc (void *memblock, size_t size, MEMFLAGS flag, const NativeCallStack& stack);
  void* realloc (void *memblock, size_t size, MEMFLAGS flag);

  // handles NULL pointers
  void  free    (void *memblock);
  char* strdup(const char *, MEMFLAGS flags = mtInternal);  // Like strdup
  // Like strdup, but exit VM when strdup() returns NULL
  char* strdup_check_oom(const char*, MEMFLAGS flags = mtInternal);

  // SocketInterface (ex HPI SocketInterface )
  int socket_close(int fd);
  int recv(int fd, char* buf, size_t nBytes, uint flags);
  int send(int fd, char* buf, size_t nBytes, uint flags);
  int raw_send(int fd, char* buf, size_t nBytes, uint flags);
  int connect(int fd, struct sockaddr* him, socklen_t len);
  hostent* get_host_by_name(char* name);

  // Support for signals (see JVM_RaiseSignal, JVM_RegisterSignal)
  void  initialize_jdk_signal_support(TRAPS);
  void  signal_notify(int signal_number);
  void* signal(int signal_number, void* handler);
  void  signal_raise(int signal_number);
  int   signal_wait();
  void* user_handler();
  void  terminate_signal_thread();
  int   sigexitnum_pd();

  // random number generation
  int random();                     // return 32bit pseudorandom number
  int next_random(unsigned int rand_seed); // pure version of random()
  void init_random(unsigned int initval);    // initialize random sequence

  // Structured OS Exception support
  void os_exception_wrapper(java_call_t f, JavaValue* value, const methodHandle& method, JavaCallArguments* args, JavaThread* thread);

  // On Posix compatible OS it will simply check core dump limits while on Windows
  // it will check if dump file can be created. Check or prepare a core dump to be
  // taken at a later point in the same thread in os::abort(). Use the caller
  // provided buffer as a scratch buffer. The status message which will be written
  // into the error log either is file location or a short error message, depending
  // on the checking result.
  void check_dump_limit(char* buffer, size_t bufferSize);

  // Get the default path to the core file
  // Returns the length of the string
  int get_core_path(char* buffer, size_t bufferSize);

  // JVMTI & JVM monitoring and management support
  // The thread_cpu_time() and current_thread_cpu_time() are only
  // supported if is_thread_cpu_time_supported() returns true.

  // Thread CPU Time - return the fast estimate on a platform
  // On Linux   - fast clock_gettime where available - user+sys
  //            - otherwise: very slow /proc fs - user+sys
  // On Windows - GetThreadTimes - user+sys
  jlong current_thread_cpu_time();
  jlong thread_cpu_time(Thread* t);

  // Thread CPU Time with user_sys_cpu_time parameter.
  //
  // If user_sys_cpu_time is true, user+sys time is returned.
  // Otherwise, only user time is returned
  jlong current_thread_cpu_time(bool user_sys_cpu_time);
  jlong thread_cpu_time(Thread* t, bool user_sys_cpu_time);

  // Return a bunch of info about the timers.
  // Note that the returned info for these two functions may be different
  // on some platforms
  void current_thread_cpu_time_info(jvmtiTimerInfo *info_ptr);
  void thread_cpu_time_info(jvmtiTimerInfo *info_ptr);

  bool is_thread_cpu_time_supported();

  // System loadavg support.  Returns -1 if load average cannot be obtained.
  int loadavg(double loadavg[], int nelem);

  // Amount beyond the callee frame size that we bang the stack.
  int extra_bang_size_in_bytes();

  char** split_path(const char* path, size_t* elements, size_t file_name_length);

  // support for mapping non-volatile memory using MAP_SYNC
  bool supports_map_sync();

  class CrashProtectionCallback : public StackObj {
  public:
    virtual void call() = 0;
  };

  // Platform dependent stuff
#ifndef _WINDOWS
# include "os_posix.hpp"
#endif
#include OS_CPU_HEADER(os)
#include OS_HEADER(os)

#ifndef OS_NATIVE_THREAD_CREATION_FAILED_MSG
#define OS_NATIVE_THREAD_CREATION_FAILED_MSG "unable to create native thread: possibly out of memory or process/resource limits reached"
#endif

#ifndef PLATFORM_PRINT_NATIVE_STACK
  // No platform-specific code for printing the native stack.
  bool platform_print_native_stack(outputStream* st, const void* context,
                                          char *buf, int buf_size) {
    return false;
  }
#endif

  // debugging support (mostly used by debug.cpp but also fatal error handler)
  bool find(address pc, outputStream* st = tty); // OS specific function to make sense out of an address

  bool dont_yield();                     // when true, JVM_Yield() is nop
  void print_statistics();

  // Thread priority helpers (implemented in OS-specific part)
  OSReturn set_native_priority(Thread* thread, int native_prio);
  OSReturn get_native_priority(const Thread* const thread, int* priority_ptr);
  int java_to_os_priority[CriticalPriority + 1];
  // Hint to the underlying OS that a task switch would not be good.
  // Void return because it's a hint and can fail.
  const char* native_thread_creation_failed_msg() {
    return OS_NATIVE_THREAD_CREATION_FAILED_MSG;
  }

  // Used at creation if requested by the diagnostic flag PauseAtStartup.
  // Causes the VM to wait until an external stimulus has been applied
  // (for Unix, that stimulus is a signal, for Windows, an external
  // ResumeThread call)
  void pause();

  // Builds a platform dependent Agent_OnLoad_<libname> function name
  // which is used to find statically linked in agents.
  char*  build_agent_function_name(const char *sym, const char *cname,
                                          bool is_absolute_path);

  class SuspendedThreadTaskContext {
  public:
    SuspendedThreadTaskContext(Thread* thread, void *ucontext) : _thread(thread), _ucontext(ucontext) {}
    Thread* thread() const { return _thread; }
    void* ucontext() const { return _ucontext; }
  private:
    Thread* _thread;
    void* _ucontext;
  };

  class SuspendedThreadTask {
  public:
    SuspendedThreadTask(Thread* thread) : _thread(thread), _done(false) {}
    void run();
    bool is_done() { return _done; }
    virtual void do_task(const SuspendedThreadTaskContext& context) = 0;
  protected:
    ~SuspendedThreadTask() {}
  private:
    void internal_do_task();
    Thread* _thread;
    bool _done;
  };

#if defined(__APPLE__) && defined(AARCH64)
  // Enables write or execute access to writeable and executable pages.
  void current_thread_enable_wx(WXMode mode);
#endif // __APPLE__ && AARCH64

#ifndef _WINDOWS
  // Suspend/resume support
  // Protocol:
  //
  // a thread starts in SR_RUNNING
  //
  // SR_RUNNING can go to
  //   * SR_SUSPEND_REQUEST when the WatcherThread wants to suspend it
  // SR_SUSPEND_REQUEST can go to
  //   * SR_RUNNING if WatcherThread decides it waited for SR_SUSPENDED too long (timeout)
  //   * SR_SUSPENDED if the stopped thread receives the signal and switches state
  // SR_SUSPENDED can go to
  //   * SR_WAKEUP_REQUEST when the WatcherThread has done the work and wants to resume
  // SR_WAKEUP_REQUEST can go to
  //   * SR_RUNNING when the stopped thread receives the signal
  //   * SR_WAKEUP_REQUEST on timeout (resend the signal and try again)
  class SuspendResume {
   public:
    enum State {
      SR_RUNNING,
      SR_SUSPEND_REQUEST,
      SR_SUSPENDED,
      SR_WAKEUP_REQUEST
    };

  private:
    volatile State _state;

  private:
    /* try to switch state from state "from" to state "to"
     * returns the state set after the method is complete
     */
    State switch_state(State from, State to);

  public:
    SuspendResume() : _state(SR_RUNNING) { }

    State state() const { return _state; }

    State request_suspend() {
      return switch_state(SR_RUNNING, SR_SUSPEND_REQUEST);
    }

    State cancel_suspend() {
      return switch_state(SR_SUSPEND_REQUEST, SR_RUNNING);
    }

    State suspended() {
      return switch_state(SR_SUSPEND_REQUEST, SR_SUSPENDED);
    }

    State request_wakeup() {
      return switch_state(SR_SUSPENDED, SR_WAKEUP_REQUEST);
    }

    State running() {
      return switch_state(SR_WAKEUP_REQUEST, SR_RUNNING);
    }

    bool is_running() const {
      return _state == SR_RUNNING;
    }

    bool is_suspended() const {
      return _state == SR_SUSPENDED;
    }
  };
#endif // !WINDOWS

  char* format_boot_path(const char* format_string,
                                const char* home,
                                int home_len,
                                char fileSep,
                                char pathSep);
  bool set_boot_path(char fileSep, char pathSep);

};

// Note that "PAUSE" is almost always used with synchronization
// so arguably we should provide Atomic::SpinPause() instead
// of the global SpinPause() with C linkage.
// It'd also be eligible for inlining on many platforms.

extern "C" int SpinPause();

#endif // SHARE_RUNTIME_OS_HPP
