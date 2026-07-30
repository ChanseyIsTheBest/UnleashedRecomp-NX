#!/usr/bin/env python3
"""Reject an UnleashedRecomp shader cache missing game-specific variants."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


EXPECTED_ENTRY_COUNT = 1385
REQUIRED_SPECIALIZATION_BITS = 0x1F

ENTRY_PATTERN = re.compile(
    r"\{\s*0x[0-9A-Fa-f]+(?:ULL)?,\s*\d+,\s*\d+,\s*\d+,\s*\d+,\s*(\d+)"
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("shader_cache", type=Path)
    args = parser.parse_args()

    source = args.shader_cache.read_text(encoding="utf-8")
    masks = [int(match.group(1)) for match in ENTRY_PATTERN.finditer(source)]
    if len(masks) != EXPECTED_ENTRY_COUNT:
        raise SystemExit(
            f"invalid shader cache: expected {EXPECTED_ENTRY_COUNT} entries, "
            f"found {len(masks)}"
        )

    combined_mask = 0
    for mask in masks:
        combined_mask |= mask

    missing = REQUIRED_SPECIALIZATION_BITS & ~combined_mask
    if missing:
        raise SystemExit(
            "invalid shader cache: XenosRecomp omitted Unleashed-specific "
            f"specialization bits 0x{missing:X} (combined mask 0x{combined_mask:X}); "
            "rebuild XenosRecomp with UNLEASHED_RECOMP defined"
        )

    print(
        f"validated {args.shader_cache}: {len(masks)} entries, "
        f"specialization mask 0x{combined_mask:X}"
    )


if __name__ == "__main__":
    main()
