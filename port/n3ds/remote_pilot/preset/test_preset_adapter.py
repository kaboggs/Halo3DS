# New3DS integration additions by Kevin Boggs.
"""Preset setup and request mapping without a device or owner content."""

import json
from pathlib import Path
import tempfile
import time
import unittest

from port.n3ds.remote_pilot.preset.halo3ds.adapter import Adapter, KEYS
from port.n3ds.remote_pilot.install_preset import install


# Kevin Boggs: FakeDevice - New3DS integration.
class FakeDevice:
    # Kevin Boggs: __init__ - New3DS integration.
    def __init__(self):
        self.calls = []
        self.sequence = 0

    # Kevin Boggs: heartbeat - New3DS integration.
    def heartbeat(self):
        return True

    # Kevin Boggs: meta - New3DS integration.
    def meta(self):
        return {"schemaVersion": 1, "gameId": "halo3ds", "deviceModel": "new",
                "bugItSequence": self.sequence}

    # Kevin Boggs: perf - New3DS integration.
    def perf(self):
        return {"timestampMs": 1, "workMs": 2.0}

    # Kevin Boggs: input - New3DS integration.
    def input(self, keys, frames):
        self.calls.append(("input", keys, frames))
        return "queued"

    # Kevin Boggs: pad - New3DS integration.
    def pad(self, payload):
        self.calls.append(("pad", payload))
        return "queued"

    # Kevin Boggs: touch - New3DS integration.
    def touch(self, payload):
        self.calls.append(("touch", payload))
        return "queued"

    # Kevin Boggs: console - New3DS integration.
    def console(self, payload):
        self.calls.append(("console", payload))
        return "ok"

    # Kevin Boggs: shot_bmp - New3DS integration.
    def shot_bmp(self):
        self.calls.append(("shot",))
        return b"image"


# Kevin Boggs: PresetTests - New3DS integration.
class PresetTests(unittest.TestCase):
    # Kevin Boggs: test_installer_only_copies_public_preset - New3DS integration.
    def test_installer_only_copies_public_preset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("server.py", "presets.py", "reference_adapter.py"):
                (root / name).touch()
            first = install(root)
            self.assertIn("installed", first)
            target = root / "presets" / "halo3ds"
            self.assertEqual({path.name for path in target.iterdir()},
                             {"manifest.json", "adapter.py", "halo_protocol.py", "catalog.json"})
            self.assertIn("already current", install(root))
            (target / "catalog.json").write_text("modified", encoding="utf-8")
            install(root)
            self.assertNotEqual((target / "catalog.json").read_text(), "modified")
            self.assertEqual(len(list((root / "local" / "preset-backups").glob("halo3ds-*"))), 1)

    # Kevin Boggs: test_pairing_and_actions - New3DS integration.
    def test_pairing_and_actions(self):
        manifest = json.loads(Path(__file__).with_name("halo3ds").joinpath("manifest.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            adapter = Adapter(directory, manifest)
            result = adapter.setup()
            self.assertEqual(Path(result["configPath"]).name, "remote-pilot-upstream-dev.cfg")
            config = Path(result["configPath"]).read_text()
            self.assertIn("session=on\n", config)
            self.assertNotIn("192.168.", config)
            fake = FakeDevice()
            adapter._client = lambda *_args, **_kwargs: fake
            adapter._host = "127.0.0.1"
            adapter._last_seen = time.monotonic()
            self.assertEqual(adapter.request("input", '{"button":"Y","frames":2}'), "queued")
            self.assertIn(("input", KEYS["Y"], 2), fake.calls)
            self.assertEqual(adapter.request("input", '{"button":"START","frames":1}'), "queued")
            self.assertIn(("input", KEYS["START"], 1), fake.calls)
            self.assertEqual(adapter.request("input", '{"button":"SELECT","frames":1}'), "queued")
            self.assertIn(("input", KEYS["SELECT"], 1), fake.calls)
            sample = {"buttons": 1, "lx": 0.5, "ly": -0.25, "rx": 0, "ry": 0,
                      "lt": 0, "rt": 1, "frames": 4}
            adapter.request("gamepad", json.dumps(sample))
            self.assertIn("buttons=0001,lx=500,ly=250,rx=0,ry=0,lt=0,rt=1000,frames=4", fake.calls[-1][1])
            adapter.request("touch", '{"x":319,"y":239,"down":true}')
            self.assertEqual(fake.calls[-1], ("touch", "x=319,y=239,down=1"))
            with self.assertRaises(ValueError):
                adapter.request("preview", "weapon:pistol")
            self.assertEqual(adapter.request("preview", "map:bloodgulch"), b"image")
            self.assertEqual(adapter.request("preview", "weapon:pistol"), b"image")
            self.assertIn(("console", "rp_preview weapon:pistol:bloodgulch"), fake.calls)

    # Kevin Boggs: test_device_capture_event - New3DS integration.
    def test_device_capture_event(self):
        manifest = json.loads(Path(__file__).with_name("halo3ds").joinpath("manifest.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            adapter = Adapter(directory, manifest)
            adapter.setup()
            fake = FakeDevice()
            adapter._client = lambda *_args, **_kwargs: fake
            observed = []
            adapter.set_event_sink(lambda *event: observed.append(event))
            config = adapter._config()
            adapter._poll("127.0.0.1", config)
            self.assertFalse(observed)
            fake.sequence = 1
            adapter._poll("127.0.0.1", config)
            self.assertEqual(observed, [("capture", 1)])


if __name__ == "__main__":
    unittest.main()
