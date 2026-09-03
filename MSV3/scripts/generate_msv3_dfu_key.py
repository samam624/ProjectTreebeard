#!/usr/bin/env python3
"""Generate one SEC1/PEM NIST P-256 key for Adafruit legacy signed DFU.

The bundled adafruit-nrfutil 0.5.3.post16 Windows executable raises a bytes/text
TypeError in its own --gen-key command. It can still read and use the resulting
PEM. This small compatibility helper uses the same `ecdsa` package and refuses
to replace an existing key.
"""

import argparse
import os
from pathlib import Path

from ecdsa import NIST256p, SigningKey


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("key_file", type=Path)
    args = parser.parse_args()
    key_file = args.key_file.resolve()
    key_file.parent.mkdir(parents=True, exist_ok=True)

    key = SigningKey.generate(curve=NIST256p, entropy=os.urandom)
    pem = key.to_pem(format="ssleay")
    try:
        with key_file.open("xb") as stream:
            stream.write(pem)
    except FileExistsError:
        parser.error(f"refusing to replace existing key: {key_file}")

    print(f"Created {key_file}")
    print("Back this file up offline before conformal coating any production board.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
