"""Generate disposable ZIP fixtures and run native / exact-oracle component tests."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile
import gzip
import io
import tarfile


def generate_zip_fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for filename, method, name in (
        ("stored.zip", zipfile.ZIP_STORED, "video.mkv"),
        ("deflated.zip", zipfile.ZIP_DEFLATED, "video.mkv"),
        ("long.zip", zipfile.ZIP_STORED, "long/" * 30 + "video.mkv"),
    ):
        with zipfile.ZipFile(directory / filename, "w", compression=method) as archive:
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = method
            archive.writestr(info, b"x" * 200000)
    with zipfile.ZipFile(directory / "multi.zip", "w") as archive:
        archive.writestr("notes.txt", b"note")
        archive.writestr("video.mkv", b"x" * 200000)
    tar_buffer = io.BytesIO()
    with tarfile.open(fileobj=tar_buffer, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        entry = tarfile.TarInfo("video.mkv")
        entry.size = 200000
        archive.addfile(entry, io.BytesIO(b"x" * entry.size))
    tar_bytes = tar_buffer.getvalue()
    (directory / "video.tar").write_bytes(tar_bytes)
    (directory / "video.tgz").write_bytes(gzip.compress(tar_bytes, mtime=0))
    (directory / "video.gz").write_bytes(gzip.compress(b"x" * 200000, mtime=0))
    raw = (directory / "stored.zip").read_bytes()
    # Actual disk-relative central-directory offsets, not just an arbitrary split.
    (directory / "cross.z01").write_bytes(raw[:100000])
    (directory / "cross.zip").write_bytes(raw[100000:])
    central_position = raw.find(b"PK\x01\x02")
    first_volume = raw[:central_position]
    second_volume = bytearray(raw[central_position:])
    eocd = second_volume.rfind(b"PK\x05\x06")
    struct.pack_into("<HH", second_volume, eocd + 4, 1, 1)
    struct.pack_into("<I", second_volume, eocd + 16, 0)
    (directory / "split.z01").write_bytes(first_volume)
    (directory / "split.zip").write_bytes(second_volume)
    (directory / "bad.zip").write_bytes(b"no zip")
    (directory / "truncated.zip").write_bytes(raw[:-30])
    position = raw.rfind(b"PK\x05\x06")
    end = bytearray(raw[position:])
    central = struct.unpack_from("<I", end, 16)[0]
    wide = struct.pack("<IQHHIIQQQQ", 0x06064B50, 44, 45, 45, 0, 0,
                       1, 1, position - central, central)
    locator = struct.pack("<IIQI", 0x07064B50, 0, position, 1)
    struct.pack_into("<H", end, 10, 65535)
    struct.pack_into("<I", end, 16, 0xFFFFFFFF)
    (directory / "zip64.zip").write_bytes(raw[:position] + wide + locator + end)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--kind", choices=("sources", "zip", "stream", "routes"), required=True)
    parser.add_argument("--oracle")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    scripts = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="server1-archive-") as temporary:
        root = Path(temporary)
        if args.kind == "sources":
            if not args.oracle:
                parser.error("source differential requires --oracle")
            golden = root / "source-oracle.json"
            subprocess.run([args.node, str(scripts / "run_archive_sources_oracle.cjs"),
                            args.oracle, str(golden)], check=True, timeout=60)
            subprocess.run([args.candidate, str(golden)], check=True, timeout=30)
        else:
            fixtures = root / "fixtures"
            generate_zip_fixtures(fixtures)
            command = [args.candidate, str(fixtures)]
            if args.oracle and args.kind == "zip":
                golden = root / "zip-oracle.json"
                subprocess.run([args.node, str(scripts / "run_zip_oracle.cjs"),
                                args.oracle, str(fixtures), str(golden)], check=True, timeout=60)
                command.append(str(golden))
            subprocess.run(command, check=True, timeout=30)


if __name__ == "__main__":
    main()
