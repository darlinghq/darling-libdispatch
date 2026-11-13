/*
 * Copyright (c) 2008-2013 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

/*
 * Swift concurrency runtime SPI of libdispatch
 * (private/swift_concurrency_private.h).
 *
 * The macOS libswift_Concurrency imports these symbols from libdispatch. A
 * libdispatch that does not define them leaves the runtime bound to another
 * libdispatch's copy of the code.
 *
 * The bodies follow apple-oss-distributions/libdispatch, release
 * libdispatch-1542.0.4 (2361ffb), src/queue.c.
 */

#include "internal.h"
#include "swift_concurrency_private.h"

/*
 * Thread QoS override support needs the pthread workqueue QoS machinery
 * (HAVE_PTHREAD_WORKQUEUE_QOS). Darling builds without it, so no libdispatch
 * thread can be overridden and the four override SPIs below report exactly
 * what the Swift runtime assumes on systems that lack them (its
 * DispatchShims.h fallback): the thread cannot be overridden, nothing was
 * applied, status 0. Apple's bodies, which differ only by calling the
 * workqueue override helpers, are kept in the HAVE_PTHREAD_WORKQUEUE_QOS
 * branches.
 */

dispatch_thread_override_info_s
dispatch_thread_get_current_override_qos_floor(void)
{
	dispatch_thread_override_info_s override_info = {0};
#if HAVE_PTHREAD_WORKQUEUE_QOS
	dispatch_qos_t override_qos_floor = _dispatch_get_basepri_override_qos_floor();
	if (override_qos_floor != DISPATCH_QOS_SATURATED) {
		override_info.can_override = true;
		override_info.override_qos_floor =
				_dispatch_qos_to_qos_class(override_qos_floor);
	}
#endif // HAVE_PTHREAD_WORKQUEUE_QOS
	return override_info;
}

int
dispatch_thread_override_self(qos_class_t override_qos)
{
#if HAVE_PTHREAD_WORKQUEUE_QOS
	dispatch_qos_t qos = _dispatch_qos_from_qos_class(override_qos);
	_dispatch_wqthread_override_start(_dispatch_tid_self(), qos);
	// ensure that the root queue sees that this thread was overridden.
	_dispatch_set_basepri_override_qos(qos);
#else
	(void)override_qos;
#endif // HAVE_PTHREAD_WORKQUEUE_QOS
	return 0;
}

int
dispatch_lock_override_start_with_debounce(dispatch_lock_t *lock_addr,
		dispatch_tid_t expected_thread, qos_class_t override_to_apply)
{
#if HAVE_PTHREAD_WORKQUEUE_QOS
	return _dispatch_wqthread_override_start_check_owner(expected_thread,
			_dispatch_qos_from_qos_class(override_to_apply), lock_addr);
#else
	(void)lock_addr; (void)expected_thread; (void)override_to_apply;
	return 0;
#endif // HAVE_PTHREAD_WORKQUEUE_QOS
}

int
dispatch_lock_override_end(qos_class_t override_to_end)
{
#if HAVE_PTHREAD_WORKQUEUE_QOS
	_dispatch_set_basepri_override_qos(
			_dispatch_qos_from_qos_class(override_to_end));
#else
	(void)override_to_end;
#endif // HAVE_PTHREAD_WORKQUEUE_QOS
	return 0;
}

bool
dispatch_verify_current_queue_4swiftonly(dispatch_queue_t dq)
{
	// Same test as dispatch_assert_queue, but answering instead of crashing:
	// true if the calling context is running on dq, false otherwise.
	unsigned long metatype = dx_metatype(dq);
	if (unlikely(metatype != _DISPATCH_LANE_TYPE &&
			metatype != _DISPATCH_WORKLOOP_TYPE)) {
		DISPATCH_CLIENT_CRASH(metatype, "invalid queue passed to "
				"dispatch_verify_current_queue_4swiftonly()");
	}

	uint64_t dq_state = os_atomic_load2o(dq, dq_state, relaxed);
	if (likely(_dq_state_drain_locked_by_self(dq_state))) {
		return true;
	}
	if (likely(_dispatch_thread_frame_find_queue(dq))) {
		return true;
	}
	return false;
}
