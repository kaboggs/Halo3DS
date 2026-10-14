# New3DS integration additions by Kevin Boggs.
# Host tool: https://github.com/kaboggs/RemotePilot
# MIT terms: LICENSE-KEVIN-MIT.md at the game source root.
"""Halo3DS RP1 client for the portable Remote Pilot receiver.

Integration API::

    adapter = HaloAdapter(device_ipv4, port, paired_token_hex)
    adapter.heartbeat()       # authenticated discovery
    adapter.meta()            # validated schemaVersion=1 mapping
    adapter.perf()            # validated mapping, or None before first frame
    adapter.log_tail()        # recent device log text
    adapter.report()          # bounded text report
    adapter.console("rp_preview map:<catalog-id>")
    adapter.input(keys=0x10, frames=4)
    adapter.pad("buttons=0001,lx=250,ly=0,rx=0,ry=0,lt=0,rt=0,frames=8")
    adapter.shot_bmp()        # stacked 400x480, 24-bit BMP bytes
    adapter.stream_av()       # stacked BMP and 16364-Hz mono PCM16 bytes
    adapter.stream_bmp()      # image-only convenience method

Each method opens one TCP connection. The adapter authenticates the device's
fresh challenge and response before accepting an action result. It never stores the token on disk,
and callers should keep errors and raw request lines out of public logs.
The game-specific wire format is documented with the device adapter at
``port/n3ds/remote_pilot/README.md`` in the Halo3DS source tree.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac
import ipaddress
import json
import math
import re
import socket
import struct
import time
from typing import Any


MAGIC = b"RP1"
HELLO = b"RP1 HELLO halo3ds\n"
CHALLENGE = re.compile(rb"RP1 CHALLENGE ([0-9a-f]{32}) ([0-9a-f]{64})\n\Z")
HEADER = re.compile(rb"RP1 ([a-z]+) (0|[1-9][0-9]{0,9}) (0|[1-9][0-9]{0,8})\n\Z")
TRAILER = re.compile(rb"RP1 MAC ([0-9a-f]{64})\n\Z")
TOKEN = re.compile(r"[0-9a-f]{64}\Z")
RESPONSE_LIMITS = {
    "none": 0,
    "result": 2048,
    "meta": 4096,
    "perf": 4096,
    "log": 4096,
    "report": 65536,
    "shot": 600000,
    "stream": 180000,
}
REQUESTS = {
    "none": "-",
    "meta": "-",
    "perf": "-",
    "log": "tail",
    "report": "capture",
    "shot": "capture",
    "stream": "capture",
}
ALLOWED_KEYS = sum(1 << bit for bit in (0, 1, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15))
PAD = re.compile(r"buttons=([0-9a-f]{4}),lx=(-?\d{1,4}),ly=(-?\d{1,4}),rx=(-?\d{1,4}),ry=(-?\d{1,4}),lt=(\d{1,4}),rt=(\d{1,4}),frames=(\d{1,2})\Z")


# Kevin Boggs: HaloAdapterError - New3DS integration.
class HaloAdapterError(Exception):
    """A connection, authentication, protocol, or remote action failed."""


# Kevin Boggs: HaloAuthenticationError - New3DS integration.
class HaloAuthenticationError(HaloAdapterError):
    """The device's challenge did not prove knowledge of the paired token."""


# Kevin Boggs: HaloProtocolError - New3DS integration.
class HaloProtocolError(HaloAdapterError):
    """The device sent an invalid, mismatched, or oversized RP1 response."""


# Kevin Boggs: HaloRemoteError - New3DS integration.
class HaloRemoteError(HaloAdapterError):
    """The authenticated device declined an action."""


# Kevin Boggs: RP1Response - New3DS integration.
@dataclass(frozen=True)
class RP1Response:
    kind: str
    request_id: int
    body: bytes


# Kevin Boggs: _read_line - New3DS integration.
def _read_line(conn: socket.socket, limit: int, deadline: float) -> bytes:
    """Read a bounded line without accidentally consuming body bytes."""
    line = bytearray()
    while len(line) < limit:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise HaloAdapterError("RP1 response timed out")
        conn.settimeout(remaining)
        part = conn.recv(1)
        if not part:
            raise HaloProtocolError("RP1 line ended early")
        line.extend(part)
        if part == b"\n":
            return bytes(line)
    raise HaloProtocolError("RP1 line exceeds limit")


# Kevin Boggs: _read_exact - New3DS integration.
def _read_exact(conn: socket.socket, length: int, deadline: float) -> bytes:
    body = bytearray()
    while len(body) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise HaloAdapterError("RP1 response timed out")
        conn.settimeout(remaining)
        part = conn.recv(min(8192, length - len(body)))
        if not part:
            raise HaloProtocolError("RP1 body ended early")
        body.extend(part)
    return bytes(body)


# Kevin Boggs: _json_object - New3DS integration.
def _json_object(body: bytes, kind: str) -> dict[str, Any]:
    if not body:
        raise HaloProtocolError(f"empty {kind} response")
    try:
        data = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HaloProtocolError(f"invalid {kind} JSON") from exc
    if not isinstance(data, dict) or type(data.get("schemaVersion")) is not int or data["schemaVersion"] != 1 or data.get("gameId") != "halo3ds":
        raise HaloProtocolError(f"invalid {kind} schema")
    if data.get("deviceModel") not in ("original", "new"):
        raise HaloProtocolError(f"invalid {kind} device model")
    return data


# Kevin Boggs: _number - New3DS integration.
def _number(value: Any) -> bool:
    if not isinstance(value, (int, float)) or isinstance(value, bool) or value < 0:
        return False
    try:
        return math.isfinite(value)
    except (OverflowError, ValueError):
        return False


# Kevin Boggs: split_rpim_audio - New3DS integration.
def split_rpim_audio(body: bytes) -> tuple[bytes, bytes]:
    """Split an optional bounded PCM trailer from a live RPIM packet."""
    if len(body) < 16 or body[:4] != b"RPIM":
        raise HaloProtocolError("invalid RPIM header")
    top_w, top_h, bottom_w, bottom_h, pixel_format, _ = struct.unpack_from("<6H", body, 4)
    if pixel_format not in (1, 2, 3):
        raise HaloProtocolError("unsupported RPIM pixel format")
    if pixel_format == 3:
        if len(body) < 20:
            raise HaloProtocolError("short RPIM RLE header")
        packed_bytes = struct.unpack_from("<I", body, 16)[0]
        image_bytes = 20 + packed_bytes
    else:
        pixel_bytes = 2 if pixel_format == 2 else 3
        image_bytes = 16 + (top_w * top_h + bottom_w * bottom_h) * pixel_bytes
    if image_bytes > 180000 or len(body) < image_bytes:
        raise HaloProtocolError("incorrect RPIM pixel length")
    if len(body) == image_bytes:
        return body, b""
    trailer = body[image_bytes:]
    if len(trailer) < 8 or trailer[:4] != b"RPAU":
        raise HaloProtocolError("invalid RPIM audio trailer")
    samples, rate = struct.unpack_from("<HH", trailer, 4)
    if rate != 16364 or samples > 8192 or len(trailer) != 8 + samples * 2:
        raise HaloProtocolError("incorrect RPIM audio length or sample rate")
    return body[:image_bytes], trailer[8:]


# Kevin Boggs: _unpack_rgb565_rle - New3DS integration.
def _unpack_rgb565_rle(data: bytes, pixels: int) -> bytes:
    """Expand bounded RGB565 runs; reject malformed lengths and trailing bytes."""
    output = bytearray(pixels * 2)
    source = target = 0
    while source < len(data) and target < len(output):
        token = data[source]
        source += 1
        count = (token & 127) + 1
        size = count * 2
        if target + size > len(output):
            raise HaloProtocolError("RPIM RLE pixel overflow")
        if token & 128:
            if source + 2 > len(data):
                raise HaloProtocolError("truncated RPIM RLE run")
            output[target:target + size] = data[source:source + 2] * count
            source += 2
        else:
            if source + size > len(data):
                raise HaloProtocolError("truncated RPIM RLE literal")
            output[target:target + size] = data[source:source + size]
            source += size
        target += size
    if target != len(output) or source != len(data):
        raise HaloProtocolError("incorrect RPIM RLE length")
    return bytes(output)


# Kevin Boggs: decode_rpim - New3DS integration.
def decode_rpim(body: bytes, *, stream: bool = False) -> bytes:
    """Validate RPIM RGB888/RGB565/RLE and return a stacked 24-bit BMP.

    Portrait buffers are rotated 90 degrees counterclockwise. The smaller
    lower image is centered under the upper image, with black side margins.
    """
    expected = (120, 200, 120, 160) if stream else (240, 400, 240, 320)
    if stream:
        body, _ = split_rpim_audio(body)
    if len(body) < 16 or body[:4] != b"RPIM":
        raise HaloProtocolError("invalid RPIM header")
    dimensions = struct.unpack_from("<6H", body, 4)
    pixel_format, orientation = dimensions[4:]
    if dimensions[:4] != expected or orientation != 1 or \
            pixel_format not in ((1, 2, 3) if stream else (1,)):
        raise HaloProtocolError("unsupported RPIM dimensions, format, or orientation")
    top_w, top_h, bottom_w, bottom_h = expected
    if pixel_format == 3:
        packed_bytes = struct.unpack_from("<I", body, 16)[0]
        if len(body) != 20 + packed_bytes:
            raise HaloProtocolError("incorrect RPIM RLE packet length")
        raw = _unpack_rgb565_rle(body[20:], top_w * top_h + bottom_w * bottom_h)
        body = body[:12] + struct.pack("<H", 2) + body[14:16] + raw
        pixel_format = 2
    pixel_bytes = 2 if pixel_format == 2 else 3
    top_bytes = top_w * top_h * pixel_bytes
    bottom_bytes = bottom_w * bottom_h * pixel_bytes
    if len(body) != 16 + top_bytes + bottom_bytes:
        raise HaloProtocolError("incorrect RPIM pixel length")

    output_w = top_h
    output_h = top_w + bottom_w
    stride = (output_w * 3 + 3) & ~3
    pixels = bytearray(stride * output_h)

    # Kevin Boggs: place - New3DS integration.
    def place(raw: memoryview, raw_w: int, raw_h: int, offset_x: int, offset_y: int) -> None:
        # PIL rotate(90, expand=True): destination (x,y) reads source
        # (raw_w - 1 - y, x) in the portrait byte buffer.
        for y in range(raw_w):
            bmp_row = output_h - 1 - (offset_y + y)
            row_start = bmp_row * stride + offset_x * 3
            source_x = raw_w - 1 - y
            for x in range(raw_h):
                src = (x * raw_w + source_x) * pixel_bytes
                dst = row_start + x * 3
                if pixel_format == 1:
                    pixels[dst:dst + 3] = raw[src:src + 3][::-1]
                else:
                    color = raw[src] | raw[src + 1] << 8
                    red = ((color >> 11) & 31) * 255 // 31
                    green = ((color >> 5) & 63) * 255 // 63
                    blue = (color & 31) * 255 // 31
                    pixels[dst:dst + 3] = bytes((blue, green, red))

    top = memoryview(body)[16:16 + top_bytes]
    bottom = memoryview(body)[16 + top_bytes:]
    place(top, top_w, top_h, 0, 0)
    place(bottom, bottom_w, bottom_h, (output_w - bottom_h) // 2, top_w)

    file_size = 54 + len(pixels)
    header = b"BM" + struct.pack("<IHHI", file_size, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, output_w, output_h, 1, 24,
                          0, len(pixels), 2835, 2835, 0, 0)
    return header + pixels


# Kevin Boggs: HaloAdapter - New3DS integration.
class HaloAdapter:
    """One-shot authenticated client for an owner-armed Halo3DS device."""

    # Kevin Boggs: __init__ - New3DS integration.
    def __init__(self, host: str, port: int, token_hex: str, timeout: float = 12.0):
        try:
            address = ipaddress.IPv4Address(host)
        except ipaddress.AddressValueError as exc:
            raise ValueError("host must be an IPv4 address") from exc
        private = any(address in network for network in (
            ipaddress.IPv4Network("10.0.0.0/8"),
            ipaddress.IPv4Network("172.16.0.0/12"),
            ipaddress.IPv4Network("192.168.0.0/16"),
            ipaddress.IPv4Network("127.0.0.0/8"),
        ))
        if not private:
            raise ValueError("host must be a private IPv4 address")
        if isinstance(port, bool) or not isinstance(port, int) or not 1024 <= port <= 65535:
            raise ValueError("port must be 1024..65535")
        if not isinstance(token_hex, str) or not TOKEN.fullmatch(token_hex):
            raise ValueError("token must be 64 lowercase hexadecimal characters")
        if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or not 0 < timeout <= 30:
            raise ValueError("timeout must be in (0, 30] seconds")
        self.host = str(address)
        self.port = port
        self._secret = bytes.fromhex(token_hex)
        self.timeout = float(timeout)

    # Kevin Boggs: request - New3DS integration.
    def request(self, kind: str, payload: str = "-", request_id: int = 0) -> RP1Response:
        """Make one authenticated exchange and enforce RP1 framing limits."""
        if kind not in (*REQUESTS, "console", "input", "pad", "touch"):
            raise ValueError("unsupported request kind")
        if isinstance(request_id, bool) or not isinstance(request_id, int) or not 0 <= request_id <= 1000000000:
            raise ValueError("invalid request id")
        if not isinstance(payload, str) or not 1 <= len(payload) <= 127 or any(ord(c) < 32 or ord(c) > 126 for c in payload):
            raise ValueError("payload must be 1..127 printable ASCII characters")
        if kind in REQUESTS and payload != REQUESTS[kind]:
            raise ValueError("incorrect fixed payload for request")
        action = f"{request_id} {kind} {payload}\n".encode("ascii")
        if len(action) > 160:
            raise ValueError("request action exceeds device limit")
        try:
            with socket.create_connection((self.host, self.port), timeout=min(self.timeout, 3.0)) as conn:
                deadline = time.monotonic() + self.timeout
                conn.settimeout(self.timeout)
                conn.sendall(HELLO)
                match = CHALLENGE.fullmatch(_read_line(conn, 128, deadline))
                if not match:
                    raise HaloProtocolError("invalid RP1 challenge")
                nonce = bytes.fromhex(match.group(1).decode("ascii"))
                proof = hmac.new(self._secret, nonce + b"server\n", hashlib.sha256).hexdigest().encode("ascii")
                if not hmac.compare_digest(match.group(2), proof):
                    raise HaloAuthenticationError("RP1 device pairing proof failed")
                mac = hmac.new(self._secret, nonce + action, hashlib.sha256).hexdigest().encode("ascii")
                conn.sendall(MAGIC + b" " + mac + b" " + action)
                header_bytes = _read_line(conn, 96, deadline)
                header = HEADER.fullmatch(header_bytes)
                if not header:
                    raise HaloProtocolError("invalid RP1 response header")
                response_kind = header.group(1).decode("ascii")
                response_id = int(header.group(2))
                length = int(header.group(3))
                if response_id != request_id or response_kind not in RESPONSE_LIMITS or length > RESPONSE_LIMITS[response_kind]:
                    raise HaloProtocolError("RP1 response id, type, or length mismatch")
                body = _read_exact(conn, length, deadline)
                trailer = TRAILER.fullmatch(_read_line(conn, 80, deadline))
                expected = hmac.new(self._secret, nonce + b"response\n" + header_bytes + body,
                                    hashlib.sha256).hexdigest().encode("ascii")
                if not trailer or not hmac.compare_digest(trailer.group(1), expected):
                    raise HaloAuthenticationError("RP1 response authentication failed")
        except (OSError, TimeoutError) as exc:
            raise HaloAdapterError("RP1 device connection failed or timed out") from exc
        return RP1Response(response_kind, response_id, body)

    # Kevin Boggs: _expect - New3DS integration.
    @staticmethod
    def _expect(response: RP1Response, kind: str) -> bytes:
        if response.kind == kind:
            return response.body
        if response.kind == "result":
            raise HaloRemoteError(response.body.decode("utf-8", "replace") or "remote action failed")
        raise HaloProtocolError(f"expected {kind} response")

    # Kevin Boggs: heartbeat - New3DS integration.
    def heartbeat(self) -> bool:
        self._expect(self.request("none"), "none")
        return True

    # Kevin Boggs: meta - New3DS integration.
    def meta(self) -> dict[str, Any]:
        data = _json_object(self._expect(self.request("meta"), "meta"), "meta")
        capabilities = data.get("capabilities")
        if not isinstance(capabilities, dict) or any(not isinstance(value, bool) for value in capabilities.values()):
            raise HaloProtocolError("invalid capability map")
        return data

    # Kevin Boggs: perf - New3DS integration.
    def perf(self) -> dict[str, Any] | None:
        body = self._expect(self.request("perf"), "perf")
        if not body:
            return None  # Game has not reached the first frame.
        data = _json_object(body, "perf")
        for key in ("timestampMs", "frame", "gameTick"):
            if isinstance(data.get(key), bool) or not isinstance(data.get(key), int) or data[key] < 0:
                raise HaloProtocolError(f"invalid {key}")
        if not isinstance(data.get("map"), str) or not re.fullmatch(r"[A-Za-z0-9_]{0,31}", data["map"]):
            raise HaloProtocolError("invalid map id")
        if data.get("mapSource") not in ("pc", "xbox") or not isinstance(data.get("menu"), bool):
            raise HaloProtocolError("invalid map source or menu state")
        for key in ("workMs", "servicesMs", "inputMs", "simulationMs", "audioMs", "renderMs"):
            if not _number(data.get(key)):
                raise HaloProtocolError(f"invalid {key}")
        for key in ("frameMs", "fps", "heapFreeBytes", "linearFreeBytes", "applicationFreeBytes"):
            if key in data and not _number(data[key]):
                raise HaloProtocolError(f"invalid {key}")
        return data

    # Kevin Boggs: log_tail - New3DS integration.
    def log_tail(self) -> str:
        return self._expect(self.request("log", "tail"), "log").decode("utf-8", "replace")

    # Kevin Boggs: report - New3DS integration.
    def report(self) -> str:
        return self._expect(self.request("report", "capture"), "report").decode("utf-8", "replace")

    # Kevin Boggs: console - New3DS integration.
    def console(self, expression: str) -> str:
        if not isinstance(expression, str) or not expression.startswith("rp_preview "):
            raise ValueError("Only catalog previews are allowed")
        response = self.request("console", expression)
        body = self._expect(response, "result").decode("ascii", "replace")
        if body not in ("ok", "rejected", "timeout", "unavailable"):
            raise HaloProtocolError("invalid console result")
        if body == "unavailable":
            raise HaloRemoteError(body)
        return body

    # Kevin Boggs: input - New3DS integration.
    def input(self, keys: int, frames: int) -> str:
        if isinstance(keys, bool) or not isinstance(keys, int) or keys < 0 or keys & ~ALLOWED_KEYS:
            raise ValueError("unsupported input key mask")
        if isinstance(frames, bool) or not isinstance(frames, int) or not 1 <= frames <= 120:
            raise ValueError("frames must be 1..120")
        body = self._expect(self.request("input", f"keys={keys:x},frames={frames}"), "result")
        status = body.decode("ascii", "replace")
        if status not in ("queued", "unavailable"):
            raise HaloProtocolError("invalid input result")
        if status == "unavailable":
            raise HaloRemoteError(status)
        return status

    # Kevin Boggs: pad - New3DS integration.
    def pad(self, payload: str) -> str:
        """Replace the current remote controller state with one bounded snapshot."""
        match = PAD.fullmatch(payload) if isinstance(payload, str) else None
        if not match:
            raise ValueError("invalid controller payload")
        buttons, lx, ly, rx, ry, lt, rt, frames = (int(value, 16) if i == 0 else int(value)
                                                 for i, value in enumerate(match.groups()))
        if any(abs(axis) > 1000 for axis in (lx, ly, rx, ry)) or not 0 <= lt <= 1000 \
                or not 0 <= rt <= 1000 or not 1 <= frames <= 30:
            raise ValueError("controller value outside device range")
        body = self._expect(self.request("pad", payload), "result")
        status = body.decode("ascii", "replace")
        if status != "queued":
            raise HaloProtocolError("invalid controller acknowledgment")
        return status

    # Kevin Boggs: shot_bmp - New3DS integration.
    def shot_bmp(self) -> bytes:
        return decode_rpim(self._expect(self.request("shot", "capture"), "shot"))

    # Kevin Boggs: touch - New3DS integration.
    def touch(self, payload: str) -> str:
        match = re.fullmatch(r'x=(\d{1,3}),y=(\d{1,3}),down=([01])', payload) if isinstance(payload, str) else None
        if not match or int(match[1]) > 319 or int(match[2]) > 239:
            raise ValueError('invalid touch payload')
        status = self._expect(self.request('touch', payload), 'result').decode('ascii', 'replace')
        if status != 'queued':
            raise HaloProtocolError('invalid touch acknowledgment')
        return status

    # Kevin Boggs: stream_av - New3DS integration.
    def stream_av(self) -> tuple[bytes, bytes]:
        body = self._expect(self.request("stream", "capture"), "stream")
        image, audio = split_rpim_audio(body)
        return decode_rpim(image, stream=True), audio

    # Kevin Boggs: stream_bmp - New3DS integration.
    def stream_bmp(self) -> bytes:
        return self.stream_av()[0]
