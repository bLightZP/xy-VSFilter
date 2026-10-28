"""Fetch pinned upstream source archives; no packages or tools are installed."""
import hashlib
import json
import pathlib
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "src/thirdparty/libass-deps"
PACKAGES = {
    "libass": "https://github.com/libass/libass/releases/download/0.17.4/libass-0.17.4.tar.xz",
    "fribidi": "https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz",
    "freetype": "https://github.com/freetype/freetype/archive/refs/tags/VER-2-14-1.tar.gz",
    "harfbuzz": "https://github.com/harfbuzz/harfbuzz/archive/refs/tags/11.5.1.tar.gz",
}

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    lock_path = DEST / "sources.lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8")) if lock_path.exists() else {}
    for name, url in PACKAGES.items():
        archive = DEST / (name + ".tar" + (".xz" if url.endswith(".xz") else ".gz"))
        if not archive.exists():
            print("Downloading", name, flush=True)
            request = urllib.request.Request(url, headers={"User-Agent": "xy-VSFilter-source-build"})
            with urllib.request.urlopen(request, timeout=90) as response:
                archive.write_bytes(response.read())
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if name in lock and lock[name] != {"url": url, "sha256": digest}:
            raise RuntimeError("Source archive does not match lock: " + name)
        lock[name] = {"url": url, "sha256": digest}
        target = DEST / "sources" / name
        if not target.exists():
            target.mkdir(parents=True)
            with tarfile.open(archive) as source:
                # Strip the upstream top directory; data_filter rejects escaping links.
                for member in source.getmembers():
                    parts = pathlib.PurePosixPath(member.name).parts
                    if len(parts) < 2:
                        continue
                    member.name = str(pathlib.PurePosixPath(*parts[1:]))
                    source.extract(member, target, filter="data")
        print(name, digest, flush=True)
    lock_path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    licenses = DEST / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, source in {
        "libass-ISC.txt": "libass/COPYING",
        "FriBidi-LGPL.txt": "fribidi/COPYING",
        "HarfBuzz.txt": "harfbuzz/COPYING",
        "FreeType-FTL.txt": "freetype/docs/FTL.TXT",
        "FreeType-GPLv2.txt": "freetype/docs/GPLv2.TXT",
    }.items():
        (licenses / name).write_bytes((DEST / "sources" / source).read_bytes())

if __name__ == "__main__":
    main()
