# New3DS integration additions by Kevin Boggs.
# Host tool: https://github.com/kaboggs/RemotePilot
# MIT terms: LICENSE-KEVIN-MIT.md at the game source root.
"""Halo3DS RP1 adapter for the portable Remote Pilot viewer.

The generic viewer supplies setup, task history, and UI. This trusted preset
translates those actions to the device build's bounded RP1 protocol.
"""

import json
from pathlib import Path
import threading
import time

from reference_adapter import ReferenceAdapter, TransportError
from .halo_protocol import HaloAdapter, HaloAdapterError


KEYS = {"A": 1 << 0, "B": 1 << 1, "SELECT": 1 << 2, "START": 1 << 3,
        "RIGHT": 1 << 4, "LEFT": 1 << 5,
        "UP": 1 << 6, "DOWN": 1 << 7, "R": 1 << 8, "L": 1 << 9,
        "X": 1 << 10, "Y": 1 << 11, "ZL": 1 << 14, "ZR": 1 << 15}


# Kevin Boggs: Adapter - New3DS integration.
class Adapter(ReferenceAdapter):
    # Kevin Boggs: __init__ - New3DS integration.
    def __init__(self, local_dir, manifest):
        super().__init__(local_dir, manifest)
        self.device_path = Path(local_dir) / "remote-pilot-upstream-dev.cfg"
        self.catalog = json.loads(Path(__file__).with_name("catalog.json").read_text(encoding="utf-8"))
        self._catalog_ids = {item["id"] for group in self.catalog["groups"] for item in group["items"]}
        self._preview_map = None
        self._io_lock = threading.Lock()

    # Kevin Boggs: _client - New3DS integration.
    def _client(self, host, config, timeout=8):
        return HaloAdapter(host, config["port"], config["token"], timeout=timeout)

    # Kevin Boggs: _probe - New3DS integration.
    def _probe(self, host, config):
        try:
            self._client(host, config, timeout=1.5).heartbeat()
            return host
        except (HaloAdapterError, ValueError):
            return None

    # Kevin Boggs: _discover - New3DS integration.
    def _discover(self, config):
        with self._lock:
            host = self._host if time.monotonic() - self._last_seen < 12 else ""
        return host or super()._discover(config)

    # Kevin Boggs: _poll - New3DS integration.
    def _poll(self, host, config):
        try:
            with self._io_lock:
                device = self._client(host, config)
                meta = device.meta()
                perf = device.perf() if self.performance_enabled() else None
        except HaloAdapterError as exc:
            raise TransportError("Paired device did not answer") from exc
        sequence = meta.get("bugItSequence", 0)
        if type(sequence) is not int or not 0 <= sequence <= 0xffffffff:
            raise TransportError("Invalid device event sequence")
        if perf is not None:
            perf.setdefault("fps", 0.0)
            perf.setdefault("frameMs", 0.0)
        with self._lock:
            prior = self._event_baseline if time.monotonic() - self._last_seen < 20 else None
            self._event_baseline = sequence
            self._host = host
            self._last_seen = time.monotonic()
            self._meta = {"label": "Halo3DS", "state": "Paired", "captureSequence": sequence,
                          "deviceModel": meta["deviceModel"]}
            self._perf = perf if self.performance_enabled() else None
            self._discovery = "Connected to Halo3DS"
        self.cache_path.write_text(json.dumps({"host": host}) + "\n", encoding="ascii")
        if prior is not None and sequence and sequence != prior and self._event_sink:
            self._event_sink("capture", sequence)

    # Kevin Boggs: _preview - New3DS integration.
    def _preview(self, device, selection):
        if selection not in self._catalog_ids:
            raise ValueError("Unknown preview item")
        if selection.startswith("map:"):
            map_id = selection[4:]
            command = selection
        else:
            if not self._preview_map:
                raise ValueError("Preview an installed PC map before a model")
            command = f"{selection}:{self._preview_map}"
            map_id = self._preview_map
        if device.console("rp_preview " + command) != "ok":
            raise ValueError("Preview needs the main menu and an installed owner map")
        self._preview_map = map_id
        time.sleep(0.8)
        return device.shot_bmp()

    # Kevin Boggs: request - New3DS integration.
    def request(self, kind, payload):
        config = self._config()
        with self._lock:
            host = self._host if time.monotonic() - self._last_seen < 20 else ""
        if not config or not host:
            raise TransportError("Paired device is not connected")
        try:
            with self._io_lock:
                device = self._client(host, config, timeout=20 if kind in ("shot", "stream", "preview") else 8)
                if kind == "shot":
                    result = device.shot_bmp()
                elif kind == "stream":
                    result = device.stream_av()
                elif kind == "report":
                    result = device.report()
                elif kind == "log":
                    result = device.log_tail()
                elif kind == "perf":
                    result = device.perf() or {}
                elif kind == "input":
                    value = json.loads(payload)
                    result = device.input(KEYS[value["button"]], value["frames"])
                elif kind == "gamepad":
                    value = json.loads(payload)
                    axes = [round(1000 * value[name] * (-1 if name in ("ly", "ry") else 1))
                            for name in ("lx", "ly", "rx", "ry")]
                    triggers = [round(1000 * value[name]) for name in ("lt", "rt")]
                    sample = (f"buttons={value['buttons']:04x},lx={axes[0]},ly={axes[1]},"
                              f"rx={axes[2]},ry={axes[3]},lt={triggers[0]},rt={triggers[1]},"
                              f"frames={value['frames']}")
                    result = device.pad(sample)
                elif kind == "touch":
                    value = json.loads(payload)
                    result = device.touch(f"x={value['x']},y={value['y']},down={int(value['down'])}")
                elif kind == "preview":
                    result = self._preview(device, payload)
                else:
                    raise ValueError("Unsupported Halo3DS action")
        except HaloAdapterError as exc:
            raise TransportError("Paired device action failed") from exc
        with self._lock:
            self._last_seen = time.monotonic()
        return result


# Kevin Boggs: create_adapter - New3DS integration.
def create_adapter(local_dir, manifest):
    return Adapter(local_dir, manifest)
