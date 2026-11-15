# Swift job regression probes

These probes are standalone Darwin executables to compile with Darling's
SDK and run inside Darling with the rebuilt libdispatch:

* `dispatch-swift-queue.c`: 128 ABI-shaped jobs on a suspended serial queue,
  followed by 128 concurrent jobs invoking themselves eight times each.
* `dispatch-data-lifetime.c`: ordinary dispatch-data regression for subranges,
  concatenation, mapped bytes and exactly-once shared-buffer destruction after
  the last owner is released. This also passes in the ARM64 guest.
* `swift-runtime-task.c`: a task allocated by the Swift 6.2.4 macOS concurrency
  runtime, submitted to a serial dispatch queue, and completed through the
  Swift async ABI. Install the matching Swift dylibs under `/usr/lib/swift`.
  `TEST_SWIFT_DIRECT=1` instead runs the task through swift_job_run as a control.
* `dispatch-swift-concurrency-spis.c`: looks up the five SPIs the Swift
  concurrency runtime imports (the four thread-override calls and
  `dispatch_verify_current_queue_4swiftonly`) and checks their contract: the
  override calls succeed and leave the thread unchanged (this libdispatch has
  no workqueue QoS, so it reports a thread that cannot be overridden), and the
  queue verification answers true only for the queue being drained (serial
  queue, sync and async), for a serial queue nested in a sync hop, and for a
  concurrent queue entered by a non-barrier `dispatch_sync_f`, which is
  recognised only through the thread's frame stack.

For example, with a Darwin-targeting Clang and a suitable SDK:

```
clang -target arm64-apple-macos11 -isysroot "$SDK" tests/dispatch-swift-queue.c -ldispatch -o dispatch-swift-queue
clang -target arm64-apple-macos11 -isysroot "$SDK" tests/swift-runtime-task.c -ldispatch -o swift-runtime-task
clang -target arm64-apple-macos11 -isysroot "$SDK" tests/dispatch-swift-concurrency-spis.c -ldispatch -o dispatch-swift-concurrency-spis
```

Each program requires its PASS message and exit status zero. The runtime
probe pins internal Swift declarations to swift-6.2.4-RELEASE; it is not a
general-purpose interface or compiler-generated Swift program. Its initial
completion takes the initial context, not that context's NULL Parent.

Observed validation: ARM64 Darling guest, serial/concurrent/re-enqueue probe,
actual runtime task and direct control; ordinary queue/semaphore/timer smoke
test; 32 libdispatch compilation units and dylib link. This used an existing
allocation-fixed guest with its matching XNU headers, not a fresh complete
Darling build. QoS is checked at the forwarding boundary, not by scheduler
timing. No claims are made for x86_64 runtime execution, arm64e, DTrace-enabled
builds, task groups, cancellation, or complete Swift application support.
