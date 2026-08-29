// ============================================================================
//  WaylandCraft-BE — compositor/Types.h
//  Shared value types between the protocol layer and the game-side registry.
// ============================================================================
#pragma once

#include "compositor/WireProtocol.h"

#include <cstdint>
#include <map>
#include <memory>

namespace wlc {

class Connection;

/// Neutral, copyable reference to a wl_surface (connection + object id).
/// The game layer (WindowRegistry / WindowDisplay / InputRouter) passes these
/// around without ever touching raw object tables.
struct SurfaceRef {
    Connection* conn = nullptr;
    ObjectId id = 0;

    bool valid() const { return conn != nullptr && id != 0; }
    bool operator==(const SurfaceRef& o) const { return conn == o.conn && id == o.id; }
    bool operator<(const SurfaceRef& o) const {
        return conn != o.conn ? conn < o.conn : id < o.id;
    }
};

struct Rect {
    int32_t x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
};

/// A committed shm/single-pixel buffer, viewed for upload into the game's
/// texture pipeline. `data` points into the client's mmap'd pool; it is
/// stable until the client destroys the buffer or resizes the pool.
struct BufferView {
    const uint8_t* data = nullptr;
    size_t mapSize = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;
    uint32_t format = ifaces::ShmFormatArgb8888;
    bool singlePixel = false; // 1x1 wp_single_pixel_buffer
    uint32_t r = 0, g = 0, b = 0, a = 0;
    bool hasData = false; // explicit validity flag set by resolveBuffer
    bool valid() const { return hasData; }
};

} // namespace wlc
