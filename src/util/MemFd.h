// ============================================================================
//  WaylandCraft-BE — util/MemFd.h
//  Anonymous, memory-backed, mappable file descriptors.
//
//  Port note (Linux -> Android): the original WaylandCraft relies on
//  wl_shm pools backed by memfd (Linux only). Android *is* Linux, but
//  bionic only exposes memfd_create officially from API 30 — LeviLaunchroid
//  targets Android 9 (API 28) — so we try, in order:
//      1. memfd_create raw syscall   (Linux 3.17+, all modern Android)
//      2. ashmem via /dev/ashmem     (Android legacy)
//      3. O_TMPFILE / unlinked file in $TMPDIR or /data/local/tmp
//      4. POSIX shm_open + shm_unlink (desktop Linux)
//  On Windows (feature-limited target, mirroring upstream "install anywhere,
//  features on Linux only") the TCP transport streams buffers inline instead
//  of sharing fds.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace wlc {

/// Creates an anonymous read/write fd of exactly `size` bytes, ready for mmap.
/// Returns -1 on failure. Caller closes the fd.
int makeMemFd(size_t size);

/// Best-effort size query for a shmem fd (ashmem-backed fds report 0 for fstat).
size_t memFdSize(int fd, size_t fallback);

} // namespace wlc
