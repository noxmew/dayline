#!/usr/bin/env python3
"""Install and audit the private release runtime. Requires Python 3.6+."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


# These are stable system interfaces, provided by the target distribution.
# Never copy glibc, the display server libraries, or graphics drivers.
SYSTEM_LIBRARIES = {
    "libc.so.6": "libc6 (>= 2.28)",
    "libm.so.6": "libc6 (>= 2.28)",
    "libpthread.so.0": "libc6 (>= 2.28)",
    "libdl.so.2": "libc6 (>= 2.28)",
    "librt.so.1": "libc6 (>= 2.28)",
    "libresolv.so.2": "libc6 (>= 2.28)",
    "libutil.so.1": "libc6 (>= 2.28)",
    "ld-linux-x86-64.so.2": "libc6 (>= 2.28)",
    "ld-linux-aarch64.so.1": "libc6 (>= 2.28)",
    "libX11.so.6": "libx11-6",
    "libX11-xcb.so.1": "libx11-xcb1",
    "libXext.so.6": "libxext6",
    "libXrender.so.1": "libxrender1",
    "libXfixes.so.3": "libxfixes3",
    "libXi.so.6": "libxi6",
    "libXrandr.so.2": "libxrandr2",
    "libXcursor.so.1": "libxcursor1",
    "libXinerama.so.1": "libxinerama1",
    "libSM.so.6": "libsm6",
    "libICE.so.6": "libice6",
    "libxcb.so.1": "libxcb1",
    "libxcb-icccm.so.4": "libxcb-icccm4",
    "libxcb-image.so.0": "libxcb-image0",
    "libxcb-keysyms.so.1": "libxcb-keysyms1",
    "libxcb-render-util.so.0": "libxcb-render-util0",
    "libxcb-randr.so.0": "libxcb-randr0",
    "libxcb-render.so.0": "libxcb-render0",
    "libxcb-shape.so.0": "libxcb-shape0",
    "libxcb-shm.so.0": "libxcb-shm0",
    "libxcb-sync.so.1": "libxcb-sync1",
    "libxcb-xfixes.so.0": "libxcb-xfixes0",
    "libxcb-xinerama.so.0": "libxcb-xinerama0",
    "libxcb-xkb.so.1": "libxcb-xkb1",
    "libxcb-xinput.so.0": "libxcb-xinput0",
    "libxcb-util.so.1": "libxcb-util1",
    "libxcb-cursor.so.0": "libxcb-cursor0",
    "libxkbcommon.so.0": "libxkbcommon0",
    "libxkbcommon-x11.so.0": "libxkbcommon-x11-0",
    "libdbus-1.so.3": "libdbus-1-3",
    "libfontconfig.so.1": "libfontconfig1",
    "libfreetype.so.6": "libfreetype6",
    "libEGL.so.1": "libegl1",
    "libGL.so.1": "libgl1",
    "libOpenGL.so.0": "libopengl0",
    "libGLESv2.so.2": "libgles2",
    "libGLX.so.0": "libglx0",
    "libGLdispatch.so.0": "libglvnd0",
    "libdrm.so.2": "libdrm2",
    "libgbm.so.1": "libgbm1",
    "libudev.so.1": "libudev1",
    "libsystemd.so.0": "libsystemd0",
    "libglib-2.0.so.0": "libglib2.0-0 | libglib2.0-0t64",
    "libgthread-2.0.so.0": "libglib2.0-0 | libglib2.0-0t64",
    "libgobject-2.0.so.0": "libglib2.0-0 | libglib2.0-0t64",
    "libgmodule-2.0.so.0": "libglib2.0-0 | libglib2.0-0t64",
    "libz.so.1": "zlib1g",
    "libexpat.so.1": "libexpat1",
    "liblzma.so.5": "liblzma5",
}


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], universal_newlines=True)


def fail(message):
    raise SystemExit(message)


def is_elf(path):
    if not path.is_file():
        return False
    with path.open("rb") as handle:
        return handle.read(4) == b"\x7fELF"


def inside(path, root):
    return os.path.commonpath([str(path.resolve()), str(root.resolve())]) == str(root.resolve())


def copy_runtime_files(source, destination, recursive=True):
    if not source.is_dir():
        fail("Missing required SDK runtime directory: " + str(source))
    destination.mkdir(parents=True, exist_ok=True)
    for item in sorted(source.iterdir()):
        if item.name.startswith(".") or item.suffix in (".a", ".prl", ".debug", ".qmltypes"):
            continue
        target = destination / item.name
        if item.is_dir():
            if recursive:
                copy_runtime_files(item, target)
        elif item.is_file():
            shutil.copy2(str(item), str(target))


def audit_elf(path):
    versions = run("readelf", "--version-info", "--wide", path)
    if "GLIBC_PRIVATE" in versions:
        fail("Private glibc ABI in " + str(path))
    for version in re.findall(r"\bGLIBC_(\d+(?:\.\d+)+)\b", versions):
        if tuple(map(int, version.split("."))) > (2, 28):
            fail("glibc baseline exceeded: {} needs GLIBC_{}".format(path, version))
    dynamic = run("readelf", "--dynamic", "--wide", path)
    needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
    if any("/" in library for library in needed):
        fail("Absolute or relative dependency path in " + str(path))
    return needed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True,
                        help="Staged /usr prefix, including DESTDIR")
    args = parser.parse_args()
    sdk = args.sdk.resolve(strict=True)
    root = args.root.resolve(strict=True)
    sdk_notice = sdk / "share/dayline-runtime"
    for metadata_name in ("complete", "build-id", "sources.tsv", "build-packages.tsv"):
        metadata_path = sdk_notice / metadata_name
        if not metadata_path.is_file() or not metadata_path.stat().st_size:
            fail("SDK is incomplete or missing provenance: " + str(metadata_path))
    runtime = root / "lib/dayline"
    libraries = runtime / "lib"
    plugins = runtime / "plugins"
    qml = runtime / "qml"
    executable = runtime / "dayline"
    if not is_elf(executable):
        fail("Missing installed Dayline ELF executable")
    if any(path.exists() for path in (libraries, plugins, qml)):
        fail("Private runtime staging directory must be clean")
    libraries.mkdir()

    # Only ship QML modules imported by Dayline and the explicitly selected
    # Basic Controls style. Other styles, examples, tools and headers stay out.
    qml_modules = (
        ("QtQml", False),
        ("QtQml/Models", True),
        ("QtQml/WorkerScript", True),
        ("QtQuick", False),
        ("QtQuick/Controls", False),
        ("QtQuick/Controls/Basic", True),
        ("QtQuick/Controls/impl", True),
        ("QtQuick/Layouts", True),
        ("QtQuick/Templates", True),
        ("QtQuick/Window", True),
    )
    for module, recursive in qml_modules:
        source = sdk / "qml" / module
        if not (source / "qmldir").is_file():
            fail("Missing SDK QML module: " + module)
        copy_runtime_files(source, qml / module, recursive)

    # Fcitx is built against this exact Qt 6, never loaded from system Qt 5.
    plugin_files = (
        "platforms/libqxcb.so", "platforms/libqoffscreen.so", "platforms/libqminimal.so",
        "sqldrivers/libqsqlite.so", "tls/libqopensslbackend.so",
        "platforminputcontexts/libcomposeplatforminputcontextplugin.so",
        "platforminputcontexts/libibusplatforminputcontextplugin.so",
        "platforminputcontexts/libfcitxplatforminputcontextplugin-qt6.so",
    )
    for name in plugin_files:
        source = sdk / "plugins" / name
        if not is_elf(source):
            fail("Missing required SDK plugin: " + str(source))
        destination = plugins / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(str(source), str(destination))
    for category in ("xcbglintegrations", "imageformats"):
        copy_runtime_files(sdk / "plugins" / category, plugins / category)
    if not is_elf(plugins / "xcbglintegrations/libqxcb-egl-integration.so"):
        fail("The private XCB runtime requires the EGL integration plugin")
    for category in ("ossl-modules", "engines-3"):
        copy_runtime_files(sdk / "lib" / category, libraries / category)

    # SDK library SONAMEs are flattened into the private lib directory. Every
    # transitive dependency must resolve either here or to a declared system ABI.
    providers = {}
    for path in sorted((sdk / "lib").glob("*.so*")):
        if not is_elf(path):
            continue
        if not inside(path, sdk):
            fail("SDK library symlink escapes its prefix: " + str(path))
        dynamic = run("readelf", "--dynamic", "--wide", path)
        match = re.search(r"\(SONAME\).*?\[(.*?)\]", dynamic)
        if match:
            soname = match.group(1)
            if soname in SYSTEM_LIBRARIES:
                fail("SDK attempts to bundle a system interface: " + soname)
            resolved = path.resolve()
            if soname in providers and providers[soname] != resolved:
                fail("Multiple SDK providers for " + soname)
            providers[soname] = resolved

    required_private = ("libstdc++.so.6", "libgcc_s.so.1", "libssl.so.3", "libcrypto.so.3", "libical.so.3")
    for soname in required_private:
        if soname not in providers:
            fail("Missing required private runtime library: " + soname)
        shutil.copy2(str(providers[soname]), str(libraries / soname))

    queue = [path for path in runtime.rglob("*") if is_elf(path)]
    scanned = set()
    # QtDBus can dlopen libdbus, so it needs an explicit package even without a
    # DT_NEEDED entry. Browser and installer actions use the system xdg tools.
    dependencies = {"ca-certificates", "tzdata", "xdg-utils", "libdbus-1-3", "libc6 (>= 2.28)"}
    while queue:
        path = queue.pop()
        if path in scanned:
            continue
        scanned.add(path)
        for soname in audit_elf(path):
            if soname in providers:
                target = libraries / soname
                if not target.exists():
                    shutil.copy2(str(providers[soname]), str(target))
                    queue.append(target)
            elif soname in SYSTEM_LIBRARIES:
                dependencies.add(SYSTEM_LIBRARIES[soname])
            else:
                fail("Unaccounted runtime dependency {} needed by {}".format(soname, path))

    for path in sorted(scanned):
        relative = os.path.relpath(str(libraries), str(path.parent))
        rpath = "$ORIGIN" if relative == "." else "$ORIGIN/" + relative
        # DT_RPATH precedes an inherited LD_LIBRARY_PATH, keeping Qt isolated
        # while still allowing the desktop's vendor GPU library search paths.
        subprocess.check_call(["patchelf", "--force-rpath", "--set-rpath", rpath, str(path)])
        subprocess.check_call(["strip", "--strip-unneeded", str(path)])
        if run("patchelf", "--print-rpath", path).strip() != rpath:
            fail("Invalid installed RPATH: " + str(path))
        audit_elf(path)

    if not (sdk_notice / "licenses").is_dir():
        fail("SDK is missing third party licenses")
    notice = root / "share/doc/dayline/runtime"
    shutil.copytree(str(sdk_notice), str(notice))
    metadata = root / "share/dayline/runtime-dependencies"
    metadata.parent.mkdir(parents=True, exist_ok=True)
    metadata.write_text(", ".join(sorted(dependencies)) + "\n", encoding="utf-8")
    print("Installed {} ELF files; glibc <= 2.28; {} external package dependencies".format(
        len(scanned), len(dependencies)))


if __name__ == "__main__":
    main()
