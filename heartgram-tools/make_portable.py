"""Builds a single-file portable HeartGram: the exe with a zip appended that
holds the python runtime and the modules folder. The client unpacks them next
to itself on the first start (extera/extera_runtime.cpp).

    python make_portable.py <HeartGram.exe> <out.exe> [--lite]

The lite marker is the zip comment, so it is the last thing in the file.
"""
import io
import os
import sys
import zipfile

LITE_MARKER = b"HEARTGRAM-LITE-BUILD"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    lite = "--lite" in sys.argv
    exe, out = args[0], args[1]
    release = os.path.dirname(os.path.abspath(exe))
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for folder in ("python", "modules"):
            root = os.path.join(release, folder)
            for base, _dirs, files in os.walk(root):
                for name in sorted(files):
                    path = os.path.join(base, name)
                    arcname = os.path.relpath(path, release).replace(os.sep, "/")
                    if arcname == "python/.runtime_sig":
                        continue
                    archive.write(path, arcname)
        if lite:
            archive.comment = LITE_MARKER
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(exe, "rb") as source, open(out, "wb") as target:
        while True:
            chunk = source.read(1 << 20)
            if not chunk:
                break
            target.write(chunk)
        target.write(buffer.getvalue())
    print(out, os.path.getsize(out) // (1024 * 1024), "MB", "lite" if lite else "full")


main()
