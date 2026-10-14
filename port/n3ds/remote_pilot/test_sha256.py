# New3DS integration additions by Kevin Boggs.
"""Host verification of the SHA-256 used by the device pairing exchange.

Run with `python port/n3ds/remote_pilot/test_sha256.py --clang PATH`.
"""

import argparse
import ctypes
import hashlib
import hmac
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "src" / "remote_pilot_sha256.c"
HEADER = HERE.parent / "src" / "remote_pilot_sha256.h"


# Kevin Boggs: run_vectors - New3DS integration.
def run_vectors(library):
    # A child process releases the loaded DLL before the parent removes its
    # temporary directory on Windows.
    hash_c = ctypes.CDLL(str(library)).host_hash
    hash_c.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
    hash_c.restype = None
    chunks_c = ctypes.CDLL(str(library)).host_hash_chunks
    chunks_c.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p]
    chunks_c.restype = None

    # Kevin Boggs: digest - New3DS integration.
    def digest(data):
        output = (ctypes.c_ubyte * 32)()
        buffer = ctypes.create_string_buffer(data)
        hash_c(buffer, len(data), output)
        return bytes(output)

    for size in (0, 1, 3, 55, 56, 63, 64, 65, 80, 96, 127, 128, 240, 256, 1024, 4096):
        data = b"abc" if size == 3 else bytes((i * 37 + 19) & 255 for i in range(size))
        assert digest(data) == hashlib.sha256(data).digest(), f"SHA-256 length {size}"
        for chunk in (1, 7, 64, 137):
            output = (ctypes.c_ubyte * 32)()
            chunks_c(ctypes.create_string_buffer(data), len(data), chunk, output)
            assert bytes(output) == hashlib.sha256(data).digest(), f"SHA-256 length {size}, chunk {chunk}"
    assert digest(b"abc").hex() == (
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    key = bytes(range(32))

    # Kevin Boggs: device_mac - New3DS integration.
    def device_mac(nonce):
        inner = bytes((x ^ 0x36) for x in key + bytes(32)) + nonce + b"server\n"
        outer = bytes((x ^ 0x5c) for x in key + bytes(32)) + digest(inner)
        return digest(outer)

    for nonce in (bytes(16), bytes(range(16))):
        assert device_mac(nonce) == hmac.new(key, nonce + b"server\n", hashlib.sha256).digest()
    assert device_mac(bytes(16)) != device_mac(bytes(range(16)))
    print("PASS: SHA-256 boundaries, FIPS vector, HMAC challenge and nonce sensitivity")


# Kevin Boggs: main - New3DS integration.
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", default=shutil.which("clang") or shutil.which("gcc"))
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.child:
        run_vectors(args.child)
        return
    if not args.clang:
        parser.error("A host C compiler is required; pass --clang PATH")
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        wrapper = root / "wrapper.c"
        library = root / ("sha256.dll" if os.name == "nt" else "sha256.so")
        wrapper.write_text(
            '#include "remote_pilot_sha256.h"\n'
            '#if defined(_WIN32)\n#define EXPORT __declspec(dllexport)\n'
            '#else\n#define EXPORT __attribute__((visibility("default")))\n#endif\n'
            'EXPORT void host_hash(const unsigned char *data, size_t size, unsigned char out[32]) '
            '{ rp_sha256(data, size, out); }\n'
            'EXPORT void host_hash_chunks(const unsigned char *data, size_t size, size_t chunk, '
            'unsigned char out[32]) { rp_sha256_ctx ctx; rp_sha256_init(&ctx); '
            'for (size_t i=0; i<size; i+=chunk) { size_t n=size-i<chunk?size-i:chunk; '
            'rp_sha256_update(&ctx,data+i,n); } rp_sha256_final(&ctx,out); }\n', encoding="ascii")
        subprocess.run([args.clang, "-shared", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", str(HEADER.parent), str(SOURCE), str(wrapper), "-o", str(library)],
                       check=True, capture_output=True, text=True)
        subprocess.run([sys.executable, str(Path(__file__).resolve()), "--child", str(library)], check=True)


if __name__ == "__main__":
    main()
