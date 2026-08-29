// ============================================================================
//  WaylandCraft-BE — compositor/WireProtocol.h
//
//  A from-scratch implementation of the Wayland wire protocol (the "Custom
//  Wayland" architecture chosen for this port — upstream WaylandCraft v2.0.3
//  used Smithay; we implement the protocol objects directly in C++ so the
//  same code serves the Linux desktop socket AND the Android relay socket).
//
//  Wire format reference (wayland.freedesktop.org/docs/html/ch05.html):
//    header : u32 object-id ; u32 (size << 16 | opcode)   little-endian
//    args   : int32 | uint32 | fixed(24.8) | string(len+bytes,NUL,pad4)
//             | object(u32) | new_id(u32) | array(len+bytes,pad4) | fd(out-of-band)
//    size includes the 8-byte header; every string/array payload includes
//    the trailing NUL (strings) and both are padded to 4-byte multiples.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wlc {

// ---------------------------------------------------------------------------
// Object ids
// ---------------------------------------------------------------------------
using ObjectId = uint32_t;

inline constexpr ObjectId kReservedIdBase = 0xff000000u; // server-reserved ids
inline constexpr ObjectId kInvalidId      = 0;

// wl_display is always object 1 per spec.
inline constexpr ObjectId kWlDisplayId = 1;

// ---------------------------------------------------------------------------
// Argument marshaling
// ---------------------------------------------------------------------------
class Writer {
public:
    void u32(uint32_t v) { append(&v, sizeof(v)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void fixed(double v) { i32(static_cast<int32_t>(v * 256.0)); }
    void objectId(ObjectId id) { u32(id); }

    void string(std::string_view s) {
        u32(static_cast<uint32_t>(s.size() + 1));
        append(s.data(), s.size());
        uint8_t nul = 0;
        append(&nul, 1);
        pad();
    }

    void array(const void* data, size_t len) {
        u32(static_cast<uint32_t>(len));
        if (len) append(data, len);
        pad();
    }

    /// Appends a raw message (already marshaled) — used for nested arrays.
    void raw(const void* data, size_t len) { append(data, len); }

    size_t size() const { return buf_.size(); }
    const uint8_t* data() const { return buf_.data(); }
    std::vector<uint8_t> take() { return std::move(buf_); }
    void clear() { buf_.clear(); }

private:
    void append(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    void pad() {
        while (buf_.size() % 4 != 0) {
            uint8_t z = 0;
            append(&z, 1);
        }
    }
    std::vector<uint8_t> buf_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len), pos_(0) {}

    bool ok() const { return pos_ <= len_; }
    size_t remaining() const { return ok() ? len_ - pos_ : 0; }

    std::optional<uint32_t> u32() {
        if (remaining() < 4) return std::nullopt;
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return v;
    }
    std::optional<int32_t> i32() {
        auto v = u32();
        if (!v) return std::nullopt;
        return static_cast<int32_t>(*v);
    }
    std::optional<double> fixed() {
        auto v = i32();
        if (!v) return std::nullopt;
        return static_cast<double>(*v) / 256.0;
    }
    std::optional<ObjectId> objectId() {
        auto v = u32();
        if (!v) return std::nullopt;
        return *v;
    }
    std::optional<std::string> string() {
        auto len = u32();
        if (!len || *len == 0 || remaining() < *len) return std::nullopt;
        size_t n = *len > 0 ? *len - 1 : 0; // strip trailing NUL
        std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += *len;
        pad(*len);
        return s;
    }
    std::optional<std::vector<uint8_t>> array() {
        auto len = u32();
        if (!len || remaining() < *len) return std::nullopt;
        std::vector<uint8_t> a(data_ + pos_, data_ + pos_ + *len);
        pos_ += *len;
        pad(*len);
        return a;
    }

    /// Skips to the next 4-byte boundary for payloads of size n.
    void pad(size_t n) {
        size_t extra = n % 4;
        if (extra) {
            size_t skip = 4 - extra;
            pos_ += (remaining() >= skip) ? skip : remaining();
        }
    }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_;
};

// ---------------------------------------------------------------------------
// Message framing
// ---------------------------------------------------------------------------
struct MessageHeader {
    ObjectId objectId = 0;
    uint16_t opcode = 0;
    uint16_t size = 0; // total message size incl. header
};

inline MessageHeader parseHeader(const uint8_t* bytes) {
    MessageHeader h;
    uint32_t id, so;
    std::memcpy(&id, bytes, 4);
    std::memcpy(&so, bytes + 4, 4);
    h.objectId = id;
    h.opcode = static_cast<uint16_t>(so & 0xffff);
    h.size = static_cast<uint16_t>(so >> 16);
    return h;
}

inline void writeHeader(Writer& w, ObjectId id, uint16_t opcode, uint16_t size) {
    w.u32(id);
    w.u32((static_cast<uint32_t>(size) << 16) | opcode);
}

// ---------------------------------------------------------------------------
// Protocol interfaces (names + opcodes). Only what we serve is listed;
// unknown requests on known objects get a clean disconnect with an error
// event, matching real compositors.
// ---------------------------------------------------------------------------
namespace ifaces {

// Core
inline constexpr const char* WlDisplay      = "wl_display";
inline constexpr const char* WlRegistry     = "wl_registry";
inline constexpr const char* WlCallback     = "wl_callback";
inline constexpr const char* WlCompositor   = "wl_compositor";
inline constexpr const char* WlSurface      = "wl_surface";
inline constexpr const char* WlRegion       = "wl_region";
inline constexpr const char* WlShm          = "wl_shm";
inline constexpr const char* WlShmPool      = "wl_shm_pool";
inline constexpr const char* WlBuffer       = "wl_buffer";
inline constexpr const char* WlSeat         = "wl_seat";
inline constexpr const char* WlPointer      = "wl_pointer";
inline constexpr const char* WlKeyboard     = "wl_keyboard";
inline constexpr const char* WlOutput       = "wl_output";
inline constexpr const char* WlSubcompositor= "wl_subcompositor";
inline constexpr const char* WlSubsurface   = "wl_subsurface";
inline constexpr const char* WlDataDeviceManager = "wl_data_device_manager";
inline constexpr const char* WlDataSource   = "wl_data_source";
inline constexpr const char* WlDataDevice   = "wl_data_device";
inline constexpr const char* WlDataOffer    = "wl_data_offer";

// xdg-shell
inline constexpr const char* XdgWmBase      = "xdg_wm_base";
inline constexpr const char* XdgPositioner  = "xdg_positioner";
inline constexpr const char* XdgSurface     = "xdg_surface";
inline constexpr const char* XdgToplevel    = "xdg_toplevel";
inline constexpr const char* XdgPopup       = "xdg_popup";

// wayland-protocols
inline constexpr const char* WpViewporter   = "wp_viewporter";
inline constexpr const char* WpViewport     = "wp_viewport";
inline constexpr const char* WpSinglePixelMgr = "wp_single_pixel_buffer_manager_v1";
inline constexpr const char* WpSinglePixelBuf = "wp_single_pixel_buffer_v1";
inline constexpr const char* WpCursorShapeMgr = "wp_cursor_shape_manager_v1";
inline constexpr const char* WpCursorShapeDev = "wp_cursor_shape_device_v1";

// wl_shm format codes (only the two the original mod renders)
inline constexpr uint32_t ShmFormatArgb8888 = 0;
inline constexpr uint32_t ShmFormatXrgb8888 = 1;

// wl_keyboard keymap format
inline constexpr uint32_t KeymapFormatNoKeymap = 0;
inline constexpr uint32_t KeymapFormatXkbV1    = 1;

// wl_pointer axis
inline constexpr uint32_t AxisVertical   = 0;
inline constexpr uint32_t AxisHorizontal = 1;

// wl_pointer button state
inline constexpr uint32_t ButtonReleased = 0;
inline constexpr uint32_t ButtonPressed  = 1;

// wl_seat capability bitmask
inline constexpr uint32_t SeatCapPointer  = 1;
inline constexpr uint32_t SeatCapKeyboard = 2;

// xdg_toplevel state enum (subset actually used by clients we target)
inline constexpr uint32_t TopStateMaximized   = 0;
inline constexpr uint32_t TopStateFullscreen  = 1;
inline constexpr uint32_t TopStateResizing    = 2;
inline constexpr uint32_t TopStateActivated   = 3;
inline constexpr uint32_t TopStateTiledLeft   = 4;
inline constexpr uint32_t TopStateTiledRight  = 5;
inline constexpr uint32_t TopStateTiledTop    = 6;
inline constexpr uint32_t TopStateTiledBottom = 7;
inline constexpr uint32_t TopStateSuspended   = 9;

// xdg_toplevel resize edge — matches upstream ResizeGrab's edge enum
inline constexpr uint32_t ResizeEdgeNone      = 0;
inline constexpr uint32_t ResizeEdgeTop       = 1;
inline constexpr uint32_t ResizeEdgeBottom    = 2;
inline constexpr uint32_t ResizeEdgeLeft      = 4;
inline constexpr uint32_t ResizeEdgeTopLeft   = 5;
inline constexpr uint32_t ResizeEdgeBottomLeft= 6;
inline constexpr uint32_t ResizeEdgeRight     = 8;
inline constexpr uint32_t ResizeEdgeTopRight  = 9;
inline constexpr uint32_t ResizeEdgeBottomRight=10;

// wl_data_device_manager dnd actions
inline constexpr uint32_t DndActionNone  = 0;
inline constexpr uint32_t DndActionCopy  = 1;
inline constexpr uint32_t DndActionMove  = 2;
inline constexpr uint32_t DndActionAsk   = 4;

} // namespace ifaces

} // namespace wlc
