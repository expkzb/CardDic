#!/usr/bin/env python3
"""Build CardDic SD-card dictionary files from UTF-8 CSV or ECDICT CSV."""

from __future__ import annotations

import argparse
import csv
import os
import struct
from dataclasses import dataclass


MAGIC = 0x43494443
VERSION = 1
FIELD_SEP = "\x1e"
RECORD_STRUCT = struct.Struct("<32sII64s")
HEADER_STRUCT = struct.Struct("<IHHI")
PREFIX_MAGIC = 0x58465043
PREFIX_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789-'"
PREFIX_HEADER_STRUCT = struct.Struct("<IHHI")
PREFIX_RECORD_STRUCT = struct.Struct("<II")


@dataclass(frozen=True)
class Row:
    key: str
    word: str
    phonetic: str
    translation: str
    example_en: str
    example_zh: str


def trunc_utf8(value: str, limit: int) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) <= limit:
        return raw
    raw = raw[:limit]
    while raw and (raw[-1] & 0xC0) == 0x80:
        raw = raw[:-1]
    return raw


def fixed_bytes(value: str, size: int) -> bytes:
    raw = trunc_utf8(value, size - 1)
    return raw + b"\0" * (size - len(raw))


def prefix_code(prefix: str) -> int:
    code = 0
    for i in range(3):
        if i < len(prefix):
            pos = PREFIX_CHARS.index(prefix[i]) + 1
        else:
            pos = 0
        code = code * (len(PREFIX_CHARS) + 1) + pos
    return code


def build_prefix_ranges(keys: list[str]) -> list[tuple[int, int]]:
    bucket_count = (len(PREFIX_CHARS) + 1) ** 3
    starts = [-1] * bucket_count
    ends = [0] * bucket_count

    for pos, key in enumerate(keys):
        for length in range(1, min(3, len(key)) + 1):
            prefix = key[:length]
            if all(c in PREFIX_CHARS for c in prefix):
                code = prefix_code(prefix)
                if starts[code] < 0:
                    starts[code] = pos
                ends[code] = pos + 1

    ranges: list[tuple[int, int]] = []
    for start, end in zip(starts, ends):
        if start < 0:
            ranges.append((0, 0))
        else:
            ranges.append((start, end - start))
    return ranges


def clean(value: str | None) -> str:
    return (value or "").strip().replace("\r", " ").replace("\n", " ")


def read_example_overrides(path: str | None) -> dict[str, tuple[str, str]]:
    if not path:
        return {}

    examples: dict[str, tuple[str, str]] = {}
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        required = {"word", "example_en", "example_zh"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise SystemExit(f"Missing example CSV columns: {', '.join(sorted(missing))}")
        for item in reader:
            word = clean(item.get("word")).lower()
            example_en = clean(item.get("example_en"))
            example_zh = clean(item.get("example_zh"))
            if word and example_en and example_zh:
                examples[word] = (example_en, example_zh)
    return examples


def read_rows(csv_path: str, limit: int | None = None, examples_path: str | None = None) -> list[Row]:
    rows: list[Row] = []
    seen: set[str] = set()
    example_overrides = read_example_overrides(examples_path)
    with open(csv_path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = set(reader.fieldnames or [])
        custom_required = {"word", "phonetic", "translation", "example_en", "example_zh"}
        ecdict_required = {"word", "phonetic", "translation"}
        if custom_required <= fieldnames:
            source = "custom"
        elif ecdict_required <= fieldnames:
            source = "ecdict"
        else:
            missing = custom_required - fieldnames
            raise SystemExit(f"Missing CSV columns: {', '.join(sorted(missing))}")

        for line_no, item in enumerate(reader, start=2):
            if limit is not None and len(rows) >= limit:
                break

            word = clean(item.get("word"))
            translation = clean(item.get("translation"))
            if source == "ecdict":
                translation = translation.replace("\\n", " / ")
                example_en, example_zh = example_overrides.get(word.lower(), ("", ""))
            else:
                example_en = clean(item.get("example_en"))
                example_zh = clean(item.get("example_zh"))

            if not word or not translation:
                continue

            key = word.lower()
            if not all(("a" <= c <= "z") or ("0" <= c <= "9") or c in "-'" for c in key):
                continue
            if len(key.encode("utf-8")) > 31:
                continue
            if key in seen:
                continue

            seen.add(key)
            rows.append(
                Row(
                    key=key,
                    word=word,
                    phonetic=clean(item.get("phonetic")),
                    translation=translation,
                    example_en=example_en,
                    example_zh=example_zh,
                )
            )
    return sorted(rows, key=lambda row: row.key)


def build(csv_path: str, out_dir: str, limit: int | None = None, examples_path: str | None = None) -> None:
    rows = read_rows(csv_path, limit, examples_path)
    if not rows:
        raise SystemExit("No valid dictionary rows found")

    os.makedirs(out_dir, exist_ok=True)
    index_path = os.path.join(out_dir, "index.bin")
    entries_path = os.path.join(out_dir, "entries.dat")
    prefix_path = os.path.join(out_dir, "prefix.bin")

    records: list[tuple[str, int, int, str]] = []
    offset = 0
    with open(entries_path, "wb") as entries:
        for row in rows:
            body = FIELD_SEP.join(
                [row.word, row.phonetic, row.translation, row.example_en, row.example_zh]
            ).encode("utf-8")
            entries.write(body)
            records.append((row.key, offset, len(body), row.translation))
            offset += len(body)

    with open(index_path, "wb") as index:
        index.write(HEADER_STRUCT.pack(MAGIC, VERSION, RECORD_STRUCT.size, len(records)))
        for key, entry_offset, entry_length, preview in records:
            index.write(
                RECORD_STRUCT.pack(
                    fixed_bytes(key, 32),
                    entry_offset,
                    entry_length,
                    fixed_bytes(preview, 64),
                )
            )

    ranges = build_prefix_ranges([record[0] for record in records])
    with open(prefix_path, "wb") as prefix:
        prefix.write(
            PREFIX_HEADER_STRUCT.pack(
                PREFIX_MAGIC,
                VERSION,
                PREFIX_RECORD_STRUCT.size,
                len(ranges),
            )
        )
        for start, count in ranges:
            prefix.write(PREFIX_RECORD_STRUCT.pack(start, count))

    print(f"wrote {len(records)} entries")
    print(index_path)
    print(entries_path)
    print(prefix_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build CardDic index.bin and entries.dat")
    parser.add_argument("csv", help="UTF-8 CSV with word,phonetic,translation,example_en,example_zh")
    parser.add_argument(
        "-o",
        "--out",
        default=os.path.join("sdcard", "carddic"),
        help="output directory, default: sdcard/carddic",
    )
    parser.add_argument("--limit", type=int, help="maximum number of valid rows to export")
    parser.add_argument("--examples", help="optional CSV that provides example_en/example_zh by word")
    args = parser.parse_args()
    build(args.csv, args.out, args.limit, args.examples)


if __name__ == "__main__":
    main()
