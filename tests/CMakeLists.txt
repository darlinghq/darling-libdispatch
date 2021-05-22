add_darling_static_library(dispatch_bsdtests
	FAT
	SOURCES
		bsdtests.c
		dispatch_test.c
)

include_directories(
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

add_compile_definitions(
	HAVE_POSIX_SPAWNP=1
)

add_darling_executable(dispatch_bsdtestharness
	bsdtestharness.c
)

target_link_libraries(dispatch_bsdtestharness
	dispatch_bsdtests
)

install(TARGETS dispatch_bsdtestharness DESTINATION libexec/darling/usr/libexec/test/dispatch)

set(TEST_COMMANDS "")

function(dispatch_add_unit_test name)
	cmake_parse_arguments(UNIT_TEST "" "" "SOURCES" ${ARGN})

	add_darling_executable(dispatch_test_${name} ${UNIT_TEST_SOURCES})
	target_link_libraries(dispatch_test_${name} dispatch_bsdtests)
	set(TEST_COMMANDS "${TEST_COMMANDS}\nrun-test ${name}" PARENT_SCOPE)
	install(TARGETS dispatch_test_${name} DESTINATION libexec/darling/usr/libexec/test/dispatch)
endfunction()

# reliable tests
set(DISPATCH_C_TESTS
	apply
	api
	debug
	queue_finalizer
	overcommit
	context_for_key
	after
	timer
	timer_short
	timer_timeout
	sema
	timer_bit31
	timer_bit63
	timer_set_time
	data
	io_muxed
	io_net
	io_pipe
	io_pipe_close
	select
)

# slightly less reliable tests
list(APPEND DISPATCH_C_TESTS
	priority
	concur
	group
	read
	read2
	starfish
	suspend_timer
	pingpong
	drift
	readsync
	cascade
	io
)

# Darwin-specific tests
list(APPEND DISPATCH_C_TESTS
	deadname
	proc
	vm
	vnode
)

foreach(test ${DISPATCH_C_TESTS})
	dispatch_add_unit_test(${test} SOURCES dispatch_${test}.c)
endforeach()

#
# tests that require special setup
#

dispatch_add_unit_test(priority2 SOURCES dispatch_priority.c)
target_compile_definitions(dispatch_test_priority2 PRIVATE
	USE_SET_TARGET_QUEUE=1
)

dispatch_add_unit_test(c99 SOURCES dispatch_c99.c)
set_target_properties(dispatch_test_c99 PROPERTIES
	C_STANDARD 99
)

dispatch_add_unit_test(plusplus SOURCES dispatch_plusplus.cpp)

#
# configure and install runner scripts
#

configure_file(darling/run-all.sh.in "${CMAKE_CURRENT_BINARY_DIR}/darling/run-all.sh" @ONLY)
install(
	PROGRAMS
		"${CMAKE_CURRENT_BINARY_DIR}/darling/run-all.sh"
		darling/run-single.sh
	DESTINATION libexec/darling/usr/libexec/test/dispatch
)
