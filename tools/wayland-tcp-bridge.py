#!/usr/bin/env python3
# ============================================================================
#  WaylandCraft-BE — tools/wayland-tcp-bridge.py
#
#  THE "Android is Linux" gateway. Minecraft Bedrock + WaylandCraft-BE runs
#  the compositor on a loopback TCP port (7231 by default). Termux apps are
#  real Linux apps that speak Wayland — but they live in a different UID, so
#  they can't reach the game's abstract namespace directly. This proxy (run
#  inside Termux) bridges a local Unix socket <-> the game's TCP port so
#  $WAYLAND_DISPLAY works exactly like on a desktop:
#
#    pkg install python xwayland-rebind   # or use socat
#    python wayland-tcp-bridge.py &
#    WAYLAND_DISPLAY=wayland-0 foot       # foot window appears IN MINECRAFT
#
#  Protocol: plain byte splicing both directions (Wayland over TCP is
#  byte-stream identical; fd passing is unavailable over TCP, which is fine —
#  the compositor serves shm via inline fallback on the TCP transport).
# ============================================================================
import socket
import sys
import threading
import socketserver

LISTEN_SOCK = "wayland-0"     # created in $XDG_RUNTIME_DIR inside Termux
GAME_HOST = "127.0.0.1"
GAME_PORT = 7231


class BridgeHandler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            upstream = socket.create_connection((GAME_HOST, GAME_PORT), timeout=5)
        except OSError as e:
            print(f"[bridge] game not reachable: {e}", file=sys.stderr)
            return

        def pump(src, dst):
            try:
                while True:
                    data = src.recv(65536)
                    if not data:
                        break
                    dst.sendall(data)
            except OSError:
                pass
            finally:
                try:
                    dst.shutdown(socket.SHUT_WR)
                except OSError:
                    pass

        t1 = threading.Thread(target=pump, args=(self.request, upstream), daemon=True)
        t2 = threading.Thread(target=pump, args=(upstream, self.request), daemon=True)
        t1.start()
        t2.start()
        t1.join()
        t2.join()


class ThreadedServer(socketserver.ThreadingUnixStreamServer):
    allow_reuse_address = True


def main():
    import os
    runtime = os.environ.get("XDG_RUNTIME_DIR", os.path.expanduser("~/.cache"))
    os.makedirs(runtime, exist_ok=True)
    path = os.path.join(runtime, LISTEN_SOCK)
    if os.path.exists(path):
        os.unlink(path)
    srv = ThreadedServer(path, BridgeHandler)
    os.chmod(path, 0o660)
    print(f"[bridge] {path} <-> {GAME_HOST}:{GAME_PORT} — WAYLAND_DISPLAY={LISTEN_SOCK}")
    srv.serve_forever()


if __name__ == "__main__":
    main()
