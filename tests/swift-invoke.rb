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
  typedef void *dispatch_invoke_context_t;
  typedef uint32_t dispatch_invoke_flags_t;
  #define DISPATCH_VTABLE_ENTRY(name) (*name)
  #define _OS_OBJECT_CLASS_HEADER() void *class_header[5]
  #define _DISPATCH_SWIFT_JOB_TYPE #{type}
  _Static_assert(_DISPATCH_SWIFT_JOB_TYPE == 1 && #{semaphore} != 1, "type collision");
  #{vtable}
  struct dispatch_swift_continuation_s {
    const struct dispatch_swift_continuation_vtable_s *do_vtable;
  };
  typedef struct dispatch_swift_continuation_s *dispatch_swift_continuation_t;
  struct regular_s;
  #{ordinary}
  struct regular_vtable {
    _OS_OBJECT_CLASS_HEADER();
    struct { DISPATCH_OBJECT_VTABLE_HEADER(regular); } _os_obj_vtable;
  };
  struct regular_s { const struct regular_vtable *do_vtable; };
  typedef union { struct regular_s *_do; struct regular_s *_dq; } dispatch_object_t;
  #define dx_vtable(x) (&(x)->do_vtable->_os_obj_vtable)
  #define dx_type(x) dx_vtable(x)->do_type
  #define dx_invoke(x,y,z) dx_vtable(x)->do_invoke(x,y,z)
  #{helper}
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
    struct dispatch_swift_continuation_s job = {metadata};
    _dispatch_object_invoke_typed((dispatch_object_t){._do=(void *)&job}, &job, 0xffffffff);
    const struct regular_vtable regular_metadata = {
      ._os_obj_vtable = { .do_type = #{semaphore}, .do_invoke = regular_call }
    };
    struct regular_s regular = {&regular_metadata};
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
puts 'PASS: typed Swift callback and preserved ordinary context/flags; controlled host objects only'
