project(libdispatch)

if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

enable_language(C CXX)

add_definitions(-Dtypeof=__typeof__ -ggdb -O0)
add_definitions(-DPRIVATE -D__PTHREAD_EXPOSE_INTERNALS__ -D__BLOCKS__ -DDISPATCH_USE_DTRACE=0 -DNeXT_RUNTIME=1 -DNS_RETURNS_INNER_POINTER=
	-DDISPATCH_USE_INTERNAL_WORKQUEUE=0
	-DDISPATCH_USE_KEVENT_WORKQUEUE=0
	-DDISPATCH_USE_KEVENT_WORKLOOP=0
	-DHAVE_PTHREAD_WORKQUEUE_QOS=0
	-DVOUCHER_USE_MACH_VOUCHER=0
	-DDISPATCH_USE_WORKQUEUE_NARROWING=0
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdinc -D__DARWIN_UNIX03 -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -std=c++11 -fPIC -nostdinc -D_Bool=bool")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/private)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src/firehose)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)

mig(src/protocol.defs)
mig(src/firehose/firehose.defs)
mig(src/firehose/firehose_reply.defs)

set(dispatch_SRCS
	resolver/resolver.c
	src/allocator.c
	src/apply.c
	src/benchmark.c
	src/block.cpp
	src/BlocksRuntime/data.c
	src/BlocksRuntime/runtime.c
	src/data.c
	src/data.m
	src/event/event.c
	src/event/event_epoll.c
	src/event/event_kevent.c
	src/event/event_windows.c
	src/eventlink.c
	src/event/workqueue.c
	# src/firehose/firehose_buffer.c
	# src/firehose/firehose_server.c
	# src/firehose/firehose_server_object.m
	src/init.c
	src/introspection.c
	src/io.c
	src/mach.c
	src/object.c
	src/object.m
	src/once.c
	src/queue.c
	src/semaphore.c
	src/shims.c
	# src/shims/generic_win_stubs.c
	# src/shims/getprogname.c
	src/shims/lock.c
	src/shims/yield.c
	src/source.c
	src/time.c
	src/transform.c
	src/voucher.c
	src/workgroup.c
	${CMAKE_CURRENT_BINARY_DIR}/src/protocolServer.c
	${CMAKE_CURRENT_BINARY_DIR}/src/protocolUser.c
)

set_source_files_properties(src/object.m
	PROPERTIES
		COMPILE_FLAGS "-Wno-return-type"
)

set(DYLIB_INSTALL_NAME "/usr/lib/system/libdispatch.dylib")

add_circular(libdispatch_shared FAT
	SOURCES
		${dispatch_SRCS}
	SIBLINGS
		system_c
		system_kernel
		system_malloc
		system_blocks
		system_pthread
		system_dyld
		system_duct
		unwind
		platform
		compiler_rt
	UPWARD
		objc
	LINK_FLAGS "-Wl,-order_file,${CMAKE_CURRENT_SOURCE_DIR}/xcodeconfig/libdispatch.order -Wl,-alias_list,${CMAKE_CURRENT_SOURCE_DIR}/xcodeconfig/libdispatch.aliases"
)

set_target_properties(libdispatch_shared PROPERTIES OUTPUT_NAME "dispatch")

add_darling_static_library(libdispatch_static SOURCES $<TARGET_OBJECTS:libdispatch_shared_obj>)

install(TARGETS libdispatch_shared DESTINATION libexec/darling/usr/lib/system)

if(ENABLE_TESTS)
	add_subdirectory(tests)
endif()
