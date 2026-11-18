#!/usr/bin/env python3
"""Exercise a host build of libmediandk.so and check its optional factory trace."""

from concurrent.futures import ThreadPoolExecutor
import ctypes
import os
from pathlib import Path
import subprocess
import sys


FACTORIES = {
    "createCodecByName": "create-by-name",
    "createDecoderByType": "create-decoder-by-type",
    "createEncoderByType": "create-encoder-by-type",
}
TRACE = "WESTLAKE_MEDIA_CODEC_TRACE"


def exercise(path):
    library = ctypes.CDLL(str(path))
    for suffix in ("", "ForClient"):
        for name in FACTORIES:
            factory = getattr(library, "AMediaCodec_" + name + suffix)
            factory.restype = ctypes.c_void_p
            factory.argtypes = [ctypes.c_char_p]
            args = [b"private-test-input"]
            if suffix:
                factory.argtypes += [ctypes.c_int, ctypes.c_uint]
                args += [os.getpid(), os.getuid()]
            with ThreadPoolExecutor(max_workers=8) as threads:
                for result in threads.map(lambda _: factory(*args), range(32)):
                    if result is not None:
                        raise RuntimeError("Unexpected codec from " + name + suffix)


def check(path):
    for value in (None, "0", "01", "1"):
        env = os.environ.copy()
        env.pop(TRACE, None)
        if value is not None:
            env[TRACE] = value
        result = subprocess.run([sys.executable, __file__, "--exercise", str(path)],
                                env=env, capture_output=True, text=True, check=True)
        expected = ["[WL-NDK-MEDIA] " + op + " unavailable" for op in FACTORIES.values()]
        if result.stderr.splitlines() != (expected if value == "1" else []):
            raise RuntimeError("Unexpected trace for setting " + repr(value))
    print("PASS: six codec factories, four trace settings, 32 calls per factory per setting")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--exercise":
        exercise(Path(sys.argv[2]).resolve(strict=True))
    elif len(sys.argv) == 2:
        check(Path(sys.argv[1]).resolve(strict=True))
    else:
        sys.exit("Usage: check_codec_factories.py path/to/host/libmediandk.so")
