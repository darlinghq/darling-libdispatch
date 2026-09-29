#!/usr/bin/env ruby
require 'tmpdir'
require 'open3'
root = File.expand_path('..', __dir__)
inline = File.read("#{root}/src/inline_internal.h")
objects = File.read("#{root}/src/object_internal.h")
queues = File.read("#{root}/src/queue_internal.h")
helper = inline[/static inline void\n_dispatch_object_invoke_typed\(.*?\n\}/m]
vtable = queues[/struct dispatch_swift_continuation_s;.*?(?=typedef struct dispatch_swift_continuation_s)/m]
ordinary = objects.scan(/^#define DISPATCH_OBJECT_VTABLE_HEADER\(x\).*?(?=^#)/m).first
enqueue = File.read("#{root}/src/queue.c")[/void\ndispatch_async_swift_job\(.*?\n\}/m]
abort 'missing enqueue implementation' unless enqueue
info = File.read("#{root}/src/introspection.c")[/static inline\ndispatch_introspection_object_s\n_dispatch_introspection_object_get_info\(.*?\n\}/m]
abort 'missing introspection helper' unless info
abort 'missing implementation' unless helper && vtable && ordinary
type = objects[/_DISPATCH_SWIFT_JOB_TYPE\s*=\s*(0x[0-9a-f]+)/, 1]
semaphore = objects[/_DISPATCH_SEMAPHORE_TYPE\s*=\s*(0x[0-9a-f]+)/, 1]
code = <<~C
  #include <stdint.h>
  #include <stdbool.h>
  #include <stddef.h>
  #include <assert.h>
  #include <stdlib.h>
  #include <string.h>
  #include <signal.h>
  #include <sys/wait.h>
  #include <sys/resource.h>
  #include <unistd.h>
  typedef void *dispatch_invoke_context_t;
  typedef uint32_t dispatch_invoke_flags_t;
  #define DISPATCH_VTABLE_ENTRY(name) (*name)
  #define _OS_OBJECT_CLASS_HEADER() void *class_header[5]
  #define _DISPATCH_SWIFT_JOB_TYPE #{type}
  _Static_assert(_DISPATCH_SWIFT_JOB_TYPE == 1 && #{semaphore} != 1, "type collision");
  #{vtable}
  struct dispatch_swift_continuation_s {
    struct regular_s *_as_do[0];
    const struct dispatch_swift_continuation_vtable_s *do_vtable;
    void *opaque_runtime_word;
  };
  typedef struct dispatch_swift_continuation_s *dispatch_swift_continuation_t;
  struct regular_s;
  #{ordinary}
  struct regular_vtable {
    _OS_OBJECT_CLASS_HEADER();
    struct { DISPATCH_OBJECT_VTABLE_HEADER(regular); } _os_obj_vtable;
  };
  struct regular_s { const struct regular_vtable *do_vtable; void *do_targetq; };
  typedef union { struct regular_s *_do; struct regular_s *_dq; void *_dc; } dispatch_object_t;
  #define dx_vtable(x) (&(x)->do_vtable->_os_obj_vtable)
  #define dx_type(x) dx_vtable(x)->do_type
  #define dx_invoke(x,y,z) dx_vtable(x)->do_invoke(x,y,z)
  typedef struct { void *object, *target_queue, *type; const char *kind; } dispatch_introspection_object_s;
  static const char *_dispatch_object_class_name(void *object) { (void)object; return "test"; }
  #{info}
  #{helper}
  typedef void *dispatch_queue_t;
  typedef unsigned dispatch_qos_class_t;
  #define unlikely(x) (x)
  #define DISPATCH_CLIENT_CRASH(type, message) abort()
  static unsigned converted, pushed;
  static void *pushed_object, *pushed_queue;
  static unsigned _dispatch_qos_from_qos_class(unsigned qos) {
    converted = qos; return qos + 100;
  }
  static void capture_push(void *queue, void *object, unsigned qos) {
    pushed_queue = queue; pushed_object = object; pushed = qos;
  }
  #define dx_push(q,j,p) capture_push(q,j,p)
  #{enqueue}
  static unsigned swift_calls, regular_calls;
  static void swift_call(struct dispatch_swift_continuation_s *job, void *ctx, uint32_t flags) {
    assert(job && ctx == NULL && flags == 0); ++swift_calls;
  }
  static void regular_call(struct regular_s *job, void *ctx, uint32_t flags) {
    assert(job && ctx == job && flags == 0x12345678); ++regular_calls;
  }
  int main(void) {
    const struct dispatch_swift_continuation_vtable_s template = {
      ._os_obj_vtable = { .do_type = 1, .do_invoke = swift_call }
    };
    /* Exact heap allocation makes an ordinary-vtable overread observable. */
    void *metadata = malloc(sizeof template); assert(metadata);
    memcpy(metadata, &template, sizeof template);
    struct dispatch_swift_continuation_s job = {.do_vtable=metadata,
      .opaque_runtime_word=metadata};
    dispatch_introspection_object_s details = _dispatch_introspection_object_get_info(
      (dispatch_object_t){._do=(void *)&job});
    assert(details.object == &job && details.type == metadata && details.target_queue == NULL);
    const unsigned priorities[] = {0, 9, 17, 21, 25, 33};
    for (unsigned i = 0; i < sizeof priorities / sizeof priorities[0]; ++i) {
      dispatch_async_swift_job(&job, &job, priorities[i]);
      assert(converted == priorities[i] && pushed == priorities[i] + 100);
      assert(pushed_object == &job && pushed_queue == &job);
    }
    _dispatch_object_invoke_typed((dispatch_object_t){._do=(void *)&job}, &job, 0xffffffff);
    const struct regular_vtable regular_metadata = {
      ._os_obj_vtable = { .do_type = #{semaphore}, .do_invoke = regular_call }
    };
    struct regular_s regular = {&regular_metadata, &job};
    details = _dispatch_introspection_object_get_info((dispatch_object_t){._do=&regular});
    assert(details.object == &regular && details.target_queue == &job);
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
      struct rlimit limit = {0, 0};
      assert(setrlimit(RLIMIT_CORE, &limit) == 0);
      dispatch_async_swift_job(&job, &regular, 21);
      _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    _dispatch_object_invoke_typed((dispatch_object_t){._do=&regular}, &regular, 0x12345678);
    assert(swift_calls == 1 && regular_calls == 1);
    free(metadata);
  }
C
Dir.mktmpdir('swift-invoke') do |dir|
  File.write("#{dir}/test.c", code)
  output, status = Open3.capture2e('clang', '-std=c11', '-O1', '-g',
    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
    "#{dir}/test.c", '-o', "#{dir}/test")
  abort output unless status.success?
  output, status = Open3.capture2e("#{dir}/test")
  abort output unless status.success?
end
puts 'PASS: typed callbacks and enqueue QoS forwarding; controlled host objects and queue adapter only'
