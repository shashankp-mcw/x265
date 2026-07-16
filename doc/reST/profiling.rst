************************
Profiling x265 with Tracy
************************

Tracy is an optional real-time profiler for investigating x265's CPU
pipeline. An instrumented x265 process streams zones, frame markers, plots,
thread names, and optional call stacks to a separate Tracy profiler. Normal
builds do not compile or link Tracy and have no profiling overhead.

The Tracy client is pinned as a source submodule. The profiler used to view a
capture must come from the same Tracy release because Tracy's network protocol
is versioned.

Building an instrumented x265
=============================

Initialize the pinned dependency and configure a dedicated build::

    git submodule update --init source/profile/tracy/vendor
    cmake -S source -B build/tracy \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DENABLE_TRACY=ON
    cmake --build build/tracy -j

``RelWithDebInfo`` provides optimized code and useful symbols. Do not compare
the speed of an instrumented binary directly with a production build; confirm
any optimization with an uninstrumented release build.

Tracy-enabled builds require CMake 3.10 or newer and a C++17-capable compiler.
These requirements do not apply when ``ENABLE_TRACY`` is disabled.

For a combined 8/10/12-bit build, pass the same profiling settings to every
stage through the multilib helper, for example::

    X265_CMAKE_ARGS="-DENABLE_TRACY=ON" build/linux/multilib.sh

The helper places one Tracy client in the combined library while retaining
instrumentation for all three bit-depth namespaces.

The following CMake settings control the client:

``ENABLE_TRACY``
    Compile Tracy instrumentation. It is disabled by default and cannot be
    combined with the PPA or VTune profiling backends.

``X265_TRACY_ON_DEMAND``
    Collect events only while a profiler is connected. This is enabled by
    default and is recommended for long encodes.

``X265_TRACY_ONLY_LOCALHOST``
    Accept profiler connections only from the encoding machine. This is the
    default. Disable it only when remote profiling is required and the network
    is trusted.

``X265_TRACY_CALLSTACK_DEPTH``
    Add a call stack of the requested depth to every x265 zone. The default is
    zero. Call stacks are valuable for a targeted investigation but add
    noticeable capture overhead.

Capturing a profile
===================

Install or build the Tracy v0.13.1 profiler from the pinned submodule or use
the matching binary from the Tracy release page. Start the profiler before the
encode, then run the instrumented CLI with a representative input and options::

    build/tracy/x265 --input sample.y4m --preset medium \
        --output sample.hevc

Connect to the x265 process in the profiler, entering ``127.0.0.1`` if the
localhost-only client is not listed by discovery. With on-demand capture
enabled, events emitted before the connection are intentionally discarded.
Start the profiler first when startup, lookahead fill, or the first frames are
relevant. Save the capture from the profiler UI when the representative
interval is complete.

Remote capture requires ``X265_TRACY_ONLY_LOCALHOST=OFF`` at configure time.
Connect to the encoder host from the profiler and permit Tracy traffic through
the host firewall. Do not expose an instrumented client on an untrusted
network.

On Linux, CPU sampling and context-switch data may require permission through
the system's ``perf_event_paranoid`` setting. Instrumented zones still work
when those operating-system facilities are unavailable.

Reading an x265 trace
=====================

The ``x265 output`` frame series advances whenever one encoded access unit is
returned. Input and output are intentionally not aligned: lookahead, B-frame
reordering, and frame threading can keep several pictures in flight.

Important zones and plots include:

``apiEncode``
    Time spent submitting input, applying back-pressure, and returning an
    access unit. Its value is the submitted POC, or the maximum 32-bit value
    during a flush call.

``frameThread`` and ``frameWait``
    Frame compression on frame-encoder threads and API back-pressure waiting
    for a frame encoder. Their values identify the POC.

``encodeRow``, ``encodeCTU``, and ``filterCTURow``
    Wavefront row work, individual CTU analysis/encoding, and loop-filter row
    work. Values identify the row or CTU address.

``slicetypeDecideEV``, ``estCostSingle``, and ``estCostCoop``
    Lookahead decisions and their serial or cooperative cost-estimation work.

``pmode``, ``pme``, ``threadedME``, and ``threadedMEWait``
    Distributed analysis, motion estimation, threaded-ME work, and dependency
    stalls.

``workerJob``
    Time a pool worker spends asking a job provider to perform available work.

``x265 active workers``
    Worker utilization over time. Repeated drops to zero within a frame often
    indicate dependency, wavefront, or reference-frame stalls.

``x265 delayed frames``, ``x265 frame bits``, and ``x265 frame QP``
    Pipeline depth and per-output-frame rate-control context.

Application information in the capture records the x265 version, dimensions,
bit depth, threading configuration, WPP state, and effective encoder options.
This makes captures self-describing when comparing presets or thread layouts.

Library use
===========

Static and shared Tracy-enabled x265 libraries contain the Tracy client and do
not change the public x265 API. When x265 is embedded in another application,
connect the profiler to that host process. x265 names only threads it owns; the
application remains responsible for naming its own API threads.

The focused integration does not instrument x265 allocators or wrap its locks.
Use the initial timeline to identify a concrete need before adding deeper,
higher-overhead instrumentation.

Further information is available from the `Tracy project
<https://github.com/wolfpld/tracy>`_ and its `v0.13.1 release
<https://github.com/wolfpld/tracy/releases/tag/v0.13.1>`_.
