#!/usr/bin/env python3
"""Decode Fab's JSON manifest (3-digit decimal byte encoding) to the
integer-based JSON format consumed by fetch_asset.py.

Fab's newer manifest endpoint returns JSON where binary fields are encoded as
concatenated 3-digit decimal bytes, e.g. a uint64 hash becomes 24 digits
(8 groups), a uint32 becomes 12 digits (4 groups), and a SHA1 becomes 60
digits (20 groups). This script reconstructs the normal manifest schema.
"""
import json
import sys


def b3(value):
    text = str(value)
    if len(text) % 3:
        raise ValueError(f"not a 3-digit byte string: {text!r}")
    return bytes(int(text[i:i + 3]) for i in range(0, len(text), 3))


def u8(value):
    return int.from_bytes(b3(value), "little")


def u32(value):
    return int.from_bytes(b3(value), "little")


def u64(value):
    return int.from_bytes(b3(value), "little")


def digest(value):
    return b3(value).hex()


def decode(source, destination):
    raw = json.load(open(source))
    manifest = {
        "ManifestFileVersion": u32(raw["ManifestFileVersion"]),
        "bIsFileData": bool(raw["bIsFileData"]),
        "AppID": str(u32(raw["AppID"])),
        "AppNameString": raw.get("AppNameString", ""),
        "BuildVersionString": raw.get("BuildVersionString", ""),
        "UninstallActionPath": raw.get("UninstallActionPath"),
        "UninstallActionArgs": raw.get("UninstallActionArgs"),
        "LaunchExeString": raw.get("LaunchExeString", ""),
        "LaunchCommand": raw.get("LaunchCommand", ""),
        "PrereqIds": raw.get("PrereqIds", []),
        "PrereqName": raw.get("PrereqName", ""),
        "PrereqPath": raw.get("PrereqPath", ""),
        "PrereqArgs": raw.get("PrereqArgs", ""),
        "FileManifestList": [],
        "ChunkHashList": {},
        "ChunkShaList": {},
        "DataGroupList": {},
        "ChunkFilesizeList": {},
        "CustomFields": raw.get("CustomFields", {}),
    }
    for item in raw["FileManifestList"]:
        manifest["FileManifestList"].append({
            "Filename": item["Filename"],
            "FileHash": digest(item["FileHash"]),
            "FileChunkParts": [
                {
                    "Guid": part["Guid"],
                    "Link": part.get("Link"),
                    "Offset": u32(part["Offset"]),
                    "Size": u32(part["Size"]),
                }
                for part in item["FileChunkParts"]
            ],
        })
    for guid, value in raw["ChunkHashList"].items():
        manifest["ChunkHashList"][guid] = u64(value)
    manifest["ChunkShaList"] = dict(raw["ChunkShaList"])
    for guid, value in raw["DataGroupList"].items():
        manifest["DataGroupList"][guid] = u8(value)
    for guid, value in raw["ChunkFilesizeList"].items():
        manifest["ChunkFilesizeList"][guid] = u64(value)

    json.dump(manifest, open(destination, "w"), indent=1)
    print(
        "decoded files", len(manifest["FileManifestList"]),
        "chunks", len(manifest["ChunkHashList"]),
        "bytes", sum(manifest["ChunkFilesizeList"].values()),
        flush=True,
    )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: decode_json_manifest.py <input.json> <output.json>")
    decode(sys.argv[1], sys.argv[2])
