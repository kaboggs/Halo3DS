# New3DS integration additions by Kevin Boggs.
"""Loopback RP1 contract tests; no game installation or device is required."""

import hashlib
import hmac
import json
import socket
import struct
import threading
import unittest

from port.n3ds.remote_pilot.preset.halo3ds.halo_protocol import (
    HaloAdapter, HaloAuthenticationError, HaloProtocolError, HaloRemoteError,
    decode_rpim,
)


TOKEN = "a" * 64
SECRET = bytes.fromhex(TOKEN)
NONCE = bytes(range(16))
META = {"schemaVersion": 1, "gameId": "halo3ds", "deviceModel": "new",
        "capabilities": {"perf": True, "shot": True}}
PERF = {"schemaVersion": 1, "gameId": "halo3ds", "deviceModel": "new",
        "timestampMs": 1234, "frame": 30, "gameTick": 10, "map": "bloodgulch",
        "mapSource": "pc", "menu": False, "workMs": 18.5, "servicesMs": 1.0,
        "inputMs": 1.0, "simulationMs": 4.0, "audioMs": 2.0, "renderMs": 10.5,
        "fps": 30.0, "frameMs": 33.33}


# Kevin Boggs: rpim - New3DS integration.
def rpim(stream=False):
    top_w, top_h, bottom_w, bottom_h = (120, 200, 120, 160) if stream else (240, 400, 240, 320)
    top = bytearray(top_w * top_h * 3)
    bottom = bytearray(bottom_w * bottom_h * 3)
    top[:3] = b"\xff\x00\x00"  # Top portrait upper-left pixel is red.
    bottom[:3] = b"\x00\x00\xff"  # Lower portrait upper-left is blue.
    return b"RPIM" + struct.pack("<6H", top_w, top_h, bottom_w, bottom_h, 1, 1) + top + bottom


# Kevin Boggs: rpim565 - New3DS integration.
def rpim565(audio=b""):
    top = bytearray(120 * 200 * 2)
    bottom = bytearray(120 * 160 * 2)
    top[:2] = b"\x00\xf8"  # Red in little-endian RGB565.
    bottom[:2] = b"\x1f\x00"  # Blue.
    image = b"RPIM" + struct.pack("<6H", 120, 200, 120, 160, 2, 1) + top + bottom
    return image + (b"RPAU" + struct.pack("<HH", len(audio) // 2, 16364) + audio if audio else b"")


# Kevin Boggs: bmp_pixel - New3DS integration.
def bmp_pixel(image, x, y):
    width, height = struct.unpack_from("<ii", image, 18)
    stride = (width * 3 + 3) & ~3
    offset = 54 + (height - 1 - y) * stride + x * 3
    return image[offset:offset + 3][::-1]


# Kevin Boggs: HaloAdapterTest - New3DS integration.
class HaloAdapterTest(unittest.TestCase):
    # Kevin Boggs: test_touch_wire_exchange_and_bounds - New3DS integration.
    def test_touch_wire_exchange_and_bounds(self):
        port, thread, errors, seen = self.serve('result',b'queued',request_kind='touch',payload='x=319,y=239,down=1')
        self.assertEqual(HaloAdapter('127.0.0.1',port,TOKEN).touch('x=319,y=239,down=1'),'queued')
        self.join_server(thread,errors)
        for payload in ('x=320,y=0,down=1','x=0,y=240,down=1','x=0,y=0,down=2','x=0,y=0,down=1\n'):
            with self.assertRaises(ValueError): HaloAdapter('127.0.0.1',port,TOKEN).touch(payload)

    # Kevin Boggs: serve - New3DS integration.
    def serve(self, response_kind="none", body=b"", *, proof=True, response_id=0,
              header=None, truncate=False, corrupt_mac=False, request_kind=None, payload=None):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        errors = []
        seen = {}

        # Kevin Boggs: worker - New3DS integration.
        def worker():
            try:
                with listener:
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(3)
                        line = bytearray()
                        while not line.endswith(b"\n"):
                            line.extend(conn.recv(1))
                        self.assertEqual(bytes(line), b"RP1 HELLO halo3ds\n")
                        mac = hmac.new(SECRET, NONCE + b"server\n", hashlib.sha256).hexdigest().encode()
                        if not proof:
                            mac = b"0" * 64
                        conn.sendall(b"RP1 CHALLENGE " + NONCE.hex().encode() + b" " + mac + b"\n")
                        if not proof:
                            return
                        line.clear()
                        while not line.endswith(b"\n"):
                            part = conn.recv(1)
                            if not part:
                                raise AssertionError("request ended early")
                            line.extend(part)
                        _, given_mac, action = bytes(line).split(b" ", 2)
                        expected = hmac.new(SECRET, NONCE + action, hashlib.sha256).hexdigest().encode()
                        self.assertEqual(given_mac, expected)
                        seen["action"] = action.decode("ascii")
                        if request_kind:
                            self.assertIn(f" {request_kind} ", seen["action"])
                        if payload:
                            self.assertTrue(seen["action"].endswith(f" {payload}\n"))
                        wire_header = header or f"RP1 {response_kind} {response_id} {len(body)}\n".encode()
                        conn.sendall(wire_header)
                        conn.sendall(body[:-1] if truncate else body)
                        if not truncate:
                            answer_mac = hmac.new(SECRET, NONCE + b"response\n" + wire_header + body,
                                                  hashlib.sha256).hexdigest().encode()
                            if corrupt_mac:
                                answer_mac = b"0" * 64
                            conn.sendall(b"RP1 MAC " + answer_mac + b"\n")
            except Exception as exc:  # Surface failures from the background thread.
                errors.append(exc)

        thread = threading.Thread(target=worker, daemon=True)
        thread.start()
        return port, thread, errors, seen

    # Kevin Boggs: join_server - New3DS integration.
    def join_server(self, thread, errors):
        thread.join(4)
        self.assertFalse(thread.is_alive())
        if errors:
            raise errors[0]

    # Kevin Boggs: test_authenticated_typed_requests - New3DS integration.
    def test_authenticated_typed_requests(self):
        cases = (
            ("none", b"", lambda a: a.heartbeat(), True),
            ("meta", json.dumps(META).encode(), lambda a: a.meta(), META),
            ("perf", json.dumps(PERF).encode(), lambda a: a.perf(), PERF),
            ("log", b"line one\nline two\n", lambda a: a.log_tail(), "line one\nline two\n"),
            ("report", b"report\n", lambda a: a.report(), "report\n"),
            ("result", b"ok", lambda a: a.console("rp_preview map:bloodgulch"), "ok"),
            ("result", b"queued", lambda a: a.input(0x10, 2), "queued"),
            ("result", b"queued", lambda a: a.pad(
                "buttons=0001,lx=250,ly=-500,rx=0,ry=0,lt=0,rt=700,frames=8"), "queued"),
        )
        for response_kind, body, invoke, expected in cases:
            with self.subTest(response_kind=response_kind, body=body[:12]):
                port, thread, errors, _ = self.serve(response_kind, body)
                adapter = HaloAdapter("127.0.0.1", port, TOKEN)
                self.assertEqual(invoke(adapter), expected)
                self.join_server(thread, errors)

    # Kevin Boggs: test_pairing_proof_and_bounds - New3DS integration.
    def test_pairing_proof_and_bounds(self):
        port, thread, errors, _ = self.serve(proof=False)
        with self.assertRaises(HaloAuthenticationError):
            HaloAdapter("127.0.0.1", port, TOKEN).heartbeat()
        self.join_server(thread, errors)
        with self.assertRaises(ValueError):
            HaloAdapter("8.8.8.8", 38763, TOKEN)
        with self.assertRaises(ValueError):
            HaloAdapter("127.0.0.1", 38763, "bad")
        with self.assertRaises(ValueError):
            HaloAdapter("127.0.0.1", 38763, TOKEN).input(1 << 3, 2)
        with self.assertRaises(ValueError):
            HaloAdapter("127.0.0.1", 38763, TOKEN).pad(
                "buttons=0001,lx=1001,ly=0,rx=0,ry=0,lt=0,rt=0,frames=8")

    # Kevin Boggs: test_response_mac_rejects_tampering - New3DS integration.
    def test_response_mac_rejects_tampering(self):
        port, thread, errors, _ = self.serve("meta", json.dumps(META).encode(), corrupt_mac=True)
        with self.assertRaises(HaloAuthenticationError):
            HaloAdapter("127.0.0.1", port, TOKEN).meta()
        self.join_server(thread, errors)

    # Kevin Boggs: test_bad_response_id_length_and_short_body - New3DS integration.
    def test_bad_response_id_length_and_short_body(self):
        for options in (
            {"response_id": 8},
            {"header": b"RP1 shot 7 600001\n"},
            {"response_id": 7, "truncate": True},
        ):
            with self.subTest(options=options):
                port, thread, errors, _ = self.serve("meta", json.dumps(META).encode(), **options)
                with self.assertRaises(HaloProtocolError):
                    HaloAdapter("127.0.0.1", port, TOKEN, timeout=1).request("meta", request_id=7)
                self.join_server(thread, errors)

    # Kevin Boggs: test_json_validation_and_remote_error - New3DS integration.
    def test_json_validation_and_remote_error(self):
        invalid = {**PERF, "workMs": float("nan")}
        port, thread, errors, _ = self.serve("perf", json.dumps(invalid).encode(), response_id=0)
        with self.assertRaises(HaloProtocolError):
            HaloAdapter("127.0.0.1", port, TOKEN).perf()
        self.join_server(thread, errors)
        port, thread, errors, _ = self.serve("result", b"unavailable", response_id=0)
        with self.assertRaises(HaloRemoteError):
            HaloAdapter("127.0.0.1", port, TOKEN).shot_bmp()
        self.join_server(thread, errors)

    # Kevin Boggs: test_rpim_rotation_color_and_centering - New3DS integration.
    def test_rpim_rotation_color_and_centering(self):
        for stream in (False, True):
            with self.subTest(stream=stream):
                image = decode_rpim(rpim(stream), stream=stream)
                self.assertEqual(image[:2], b"BM")
                width, height = struct.unpack_from("<ii", image, 18)
                self.assertEqual((width, height), (200, 240) if stream else (400, 480))
                top_y = 119 if stream else 239
                bottom_x = 20 if stream else 40
                bottom_y = 239 if stream else 479
                self.assertEqual(bmp_pixel(image, 0, top_y), b"\xff\x00\x00")
                self.assertEqual(bmp_pixel(image, bottom_x, bottom_y), b"\x00\x00\xff")
                self.assertEqual(bmp_pixel(image, 0, bottom_y), b"\x00\x00\x00")
                bad = bytearray(rpim(stream))
                bad[12] = 3
                with self.assertRaises(HaloProtocolError):
                    decode_rpim(bytes(bad), stream=stream)

    # Kevin Boggs: test_rgb565_live_preview - New3DS integration.
    def test_rgb565_live_preview(self):
        image = decode_rpim(rpim565(), stream=True)
        self.assertEqual(bmp_pixel(image, 0, 119), b"\xff\x00\x00")
        self.assertEqual(bmp_pixel(image, 20, 239), b"\x00\x00\xff")
        with self.assertRaises(HaloProtocolError):
            decode_rpim(rpim565()[:-2], stream=True)
        port, thread, errors, _ = self.serve("stream", rpim565(), request_kind="stream")
        self.assertEqual(HaloAdapter("127.0.0.1", port, TOKEN).stream_bmp(), image)
        self.join_server(thread, errors)

    # Kevin Boggs: test_rgb565_live_audio_trailer - New3DS integration.
    def test_rgb565_live_audio_trailer(self):
        pcm = struct.pack("<4h", 0, 1234, -1234, 32767)
        port, thread, errors, _ = self.serve("stream", rpim565(pcm), request_kind="stream")
        image, audio = HaloAdapter("127.0.0.1", port, TOKEN).stream_av()
        self.assertEqual(image, decode_rpim(rpim565(), stream=True))
        self.assertEqual(audio, pcm)
        self.join_server(thread, errors)
        with self.assertRaises(HaloProtocolError):
            decode_rpim(rpim565(pcm)[:-2], stream=True)
        invalid = bytearray(rpim565(pcm))
        invalid[len(rpim565()):len(rpim565()) + 4] = b"BAD!"
        with self.assertRaises(HaloProtocolError):
            decode_rpim(bytes(invalid), stream=True)

    # Kevin Boggs: test_screenshot_and_stream_exchange - New3DS integration.
    def test_screenshot_and_stream_exchange(self):
        for stream in (False, True):
            with self.subTest(stream=stream):
                port, thread, errors, seen = self.serve("stream" if stream else "shot", rpim(stream),
                                                         response_id=0, request_kind="stream" if stream else "shot",
                                                         payload="capture")
                adapter = HaloAdapter("127.0.0.1", port, TOKEN)
                image = adapter.stream_bmp() if stream else adapter.shot_bmp()
                self.assertTrue(image.startswith(b"BM"))
                self.assertIn(" capture\n", seen["action"])
                self.join_server(thread, errors)


if __name__ == "__main__":
    unittest.main()
