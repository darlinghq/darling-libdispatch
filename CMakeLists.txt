project(libdispatch)

if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

enable_language(C CXX)

add_definitions(-Dtypeof=__typeof__ -ggdb -O0)
add_definitions(-DPRIVATE -DDISPATCH_USE_DTRACE=0 -DNeXT_RUNTIME=1 -DNS_RETURNS_INNER_POINTER=
	-DDISPATCH_USE_KEVENT_WORKQUEUE=0
	-DDISPATCH_USE_KEVENT_QOS=0
	-DHAVE_PTHREAD_WORKQUEUE_QOS=0
	-DDISPATCH_USE_MEMORYSTATUS=1
	-DUSE_APPLE_TSD_OPTIMIZATIONS=1
	-DHAVE_PTHREAD_KEY_INIT_NP=1
	-DDISPATCH_USE_VM_PRESSURE=0
	-DVOUCHER_USE_MACH_VOUCHER=0
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdinc -D__DARWIN_UNIX03 -fPIC -mmacosx-version-min=10.12")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -std=c++11 -fPIC -mmacosx-version-min=10.12 -nostdinc -D_Bool=bool")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/private)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src/firehose)
include_directories(${CMAKE_SOURCE_DIR}/src/libmalloc/include)
include_directories(${CMAKE_SOURCE_DIR}/src/kernel/libsyscall)
include_directories(${CMAKE_SOURCE_DIR}/src/duct/include)
include_directories(${CMAKE_SOURCE_DIR}/src/external/libclosure)
include_directories(${CMAKE_SOURCE_DIR}/src/external/libpthread)
include_directories(${CMAKE_SOURCE_DIR}/src/external/libpthread/private)
include_directories(${CMAKE_SOURCE_DIR}/src/kernel/libsyscall/wrappers/libproc)
include_directories(${CMAKE_SOURCE_DIR}/src/external/objc4/runtime)
include_directories(${CMAKE_SOURCE_DIR}/src/external/foundation/include)
include_directories(${CMAKE_SOURCE_DIR}/src/external/corefoundation)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/external/libcxx/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)

mig(src/protocol.defs)
mig(src/firehose/firehose.defs)
mig(src/firehose/firehose_reply.defs)

set(dispatch_SRCS
	src/allocator.c
	src/apply.c
	src/benchmark.c
	src/data.c
	src/datam.m
	src/init.c
	src/introspection.c
	src/io.c
	src/object.c
	src/objectm.m
	src/once.c
	src/queue.c
	src/semaphore.c
	src/source.c
	src/time.c
	src/transform.c
	src/voucher.c
	src/block.cpp
	src/shims/lock.c
	#src/firehose/firehose_buffer.c
	${CMAKE_CURRENT_BINARY_DIR}/src/protocolServer.c
	${CMAKE_CURRENT_BINARY_DIR}/src/protocolUser.c
	#${CMAKE_CURRENT_BINARY_DIR}/src/firehose/firehoseUser.c
	#${CMAKE_CURRENT_BINARY_DIR}/src/firehose/firehose_replyServer.c
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

add_library(libdispatch_static STATIC $<TARGET_OBJECTS:libdispatch_shared_obj>)

install(TARGETS libdispatch_shared DESTINATION libexec/darling/usr/lib/system)

