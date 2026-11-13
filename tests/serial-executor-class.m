/*
 * Copyright (C) 2026 Lubos Dolezel
 *
 * Checks the Objective-C class hierarchy of dispatch queues and that the Swift
 * job entry point is reachable through libSystem:
 *   OS_dispatch_queue_serial_executor exists, derives from OS_dispatch_queue,
 *   and is the superclass of OS_dispatch_queue_serial and OS_dispatch_workloop;
 *   OS_dispatch_queue_main derives from OS_dispatch_queue_serial;
 *   OS_dispatch_queue_concurrent and OS_dispatch_queue_global stay direct
 *   subclasses of OS_dispatch_queue;
 *   dispatch_async_swift_job is exported by libdispatch and bound via libSystem.
 * Black-box reference: macOS 26.7.3, same program.
 */
#import <objc/NSObject.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static const char *super_name(const char *name)
{
	Class c = objc_getClass(name);
	if (!c) return NULL;
	Class s = class_getSuperclass(c);
	return s ? class_getName(s) : "";
}

static void expect_super(const char *name, const char *expected)
{
	const char *s = super_name(name);
	if (!s || strcmp(s, expected)) {
		printf("FAIL %s: superclass %s, expected %s\n", name, s ? s : "(no class)", expected);
		failures++;
	}
}

int main(void)
{
	{
		expect_super("OS_dispatch_queue_serial_executor", "OS_dispatch_queue");
		expect_super("OS_dispatch_queue_serial", "OS_dispatch_queue_serial_executor");
		expect_super("OS_dispatch_workloop", "OS_dispatch_queue_serial_executor");
		expect_super("OS_dispatch_queue_main", "OS_dispatch_queue_serial");
		expect_super("OS_dispatch_queue_concurrent", "OS_dispatch_queue");
		expect_super("OS_dispatch_queue_global", "OS_dispatch_queue");

		Class exec = objc_getClass("OS_dispatch_queue_serial_executor");
		CHECK(exec != Nil);

		dispatch_queue_t serial = dispatch_queue_create("v341.serial", DISPATCH_QUEUE_SERIAL);
		dispatch_queue_t conc = dispatch_queue_create("v341.concurrent", DISPATCH_QUEUE_CONCURRENT);
		CHECK([(id)serial isKindOfClass:exec]);
		CHECK([(id)dispatch_get_main_queue() isKindOfClass:exec]);
		CHECK(![(id)conc isKindOfClass:exec]);
		CHECK(![(id)dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0) isKindOfClass:exec]);
#if !OS_OBJECT_USE_OBJC_RETAIN_RELEASE
		dispatch_release(serial);
		dispatch_release(conc);
#endif
	}

	void *libsystem = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOW);
	CHECK(libsystem != NULL);
	void *via_system = libsystem ? dlsym(libsystem, "dispatch_async_swift_job") : NULL;
	void *via_dispatch = dlsym(RTLD_DEFAULT, "dispatch_async_swift_job");
	void *plain = dlsym(RTLD_DEFAULT, "dispatch_async_f");
	CHECK(via_system != NULL);
	CHECK(via_dispatch != NULL);
	CHECK(via_system == via_dispatch);
	CHECK(plain != NULL && plain != via_dispatch);

	if (failures) { printf("FAILED %d checks\n", failures); return 1; }
	puts("PASS serial executor class hierarchy and dispatch_async_swift_job export");
	return 0;
}
