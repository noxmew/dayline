#!/usr/bin/env bash
set -Eeuo pipefail

trap 'printf "AMD64 runtime SDK failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR

[[ -f /.dockerenv ]] || { echo 'Run inside the AMD64 runtime SDK container.' >&2; exit 1; }
source /etc/os-release
[[ "$ID" == rocky && "$VERSION_ID" == 8.* && "$EUID" == 0 && "$(uname -m)" == x86_64 ]] || {
    echo 'This recipe requires a root AMD64 Rocky Linux 8 container.' >&2; exit 1;
}
[[ "$(getconf GNU_LIBC_VERSION)" == 'glibc 2.28' ]] || {
    echo 'The SDK requires the glibc 2.28 build baseline.' >&2; exit 1;
}

SDK_PREFIX=${SDK_PREFIX:-/sdk/prefix}
SDK_WORKDIR=${SDK_WORKDIR:-/tmp/dayline-runtime-build}
SDK_JOBS=${SDK_JOBS:-1}
[[ "$SDK_PREFIX" == /* && "$SDK_WORKDIR" == /* && "$SDK_PREFIX" != / && "$SDK_WORKDIR" != / ]] || {
    echo 'SDK_PREFIX and SDK_WORKDIR must be non-root absolute paths.' >&2; exit 1;
}
[[ "$SDK_JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'SDK_JOBS must be a positive integer.' >&2; exit 1; }
if [[ -d "$SDK_PREFIX" && -n "$(find "$SDK_PREFIX" -mindepth 1 -print -quit)" ]]; then
    echo 'This recipe requires an empty SDK_PREFIX.' >&2; exit 1
fi
rpm -q gcc-toolset-12-gcc-c++ cmake ninja-build libarchive openssl-devel >/dev/null
[[ "$(rpm -q --qf '%{VERSION}' openssl-libs)" == 1.1.1* ]] || {
    echo 'The official Qt TLS plugin requires the maintained RHEL 8 OpenSSL 1.1 runtime.' >&2; exit 1;
}

# Dependencies are installed by Dockerfile.runtime-sdk-amd64. Keep all source
# downloads and the three small build trees on that Dockerfile's /tmp tmpfs.
set +u
source /opt/rh/gcc-toolset-12/enable
set -u
export PATH="$SDK_PREFIX/bin:$PATH"
export CMAKE_PREFIX_PATH="$SDK_PREFIX"
export PKG_CONFIG_PATH="$SDK_PREFIX/lib/pkgconfig"
export LD_LIBRARY_PATH="$SDK_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

metadata="$SDK_PREFIX/share/dayline-runtime"
mkdir -p "$metadata/licenses" "$SDK_WORKDIR/downloads" "$SDK_WORKDIR/src" "$SDK_WORKDIR/build"
build_id=$(printf '%s\n%s\n%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | cut -d ' ' -f 1)" \
    "$(uname -m)" "$SDK_PREFIX" | sha256sum | cut -d ' ' -f 1)
printf '%s\n' "$build_id" > "$metadata/build-id"
printf 'component\tsha256\turl\n' > "$metadata/sources.tsv"
install -m 644 "${BASH_SOURCE[0]}" "$metadata/build-runtime-sdk-amd64.sh"

fetch_archive() {
    local name=$1 url=$2 checksum=$3 filename=$4
    FETCHED_ARCHIVE="$SDK_WORKDIR/downloads/$filename"
    curl --fail --show-error --location --retry 3 --output "$FETCHED_ARCHIVE" "$url"
    printf '%s  %s\n' "$checksum" "$FETCHED_ARCHIVE" | sha256sum --check --status
    printf '%s\t%s\t%s\n' "$name" "$checksum" "$url" >> "$metadata/sources.tsv"
}

extract_qt_archive() {
    python3 - "$FETCHED_ARCHIVE" "$SDK_PREFIX" "$1" <<'PY'
import ctypes as c
import os
from pathlib import Path, PurePosixPath
import stat
import sys

archive_name, prefix_name, archive_prefix = sys.argv[1:]
prefix = Path(prefix_name).resolve()
strip_parts = PurePosixPath(archive_prefix).parts if archive_prefix else ()
lib = c.CDLL('libarchive.so.13')
lib.archive_read_new.restype = c.c_void_p
for name in ('archive_read_support_filter_all', 'archive_read_support_format_7zip', 'archive_read_free'):
    getattr(lib, name).argtypes = [c.c_void_p]
lib.archive_read_open_filename.argtypes = [c.c_void_p, c.c_char_p, c.c_size_t]
lib.archive_read_next_header.argtypes = [c.c_void_p, c.POINTER(c.c_void_p)]
lib.archive_read_data_into_fd.argtypes = [c.c_void_p, c.c_int]
for name in ('archive_entry_pathname', 'archive_entry_symlink', 'archive_error_string'):
    getattr(lib, name).argtypes = [c.c_void_p]
    getattr(lib, name).restype = c.c_char_p
for name in ('archive_entry_filetype', 'archive_entry_perm'):
    getattr(lib, name).argtypes = [c.c_void_p]
    getattr(lib, name).restype = c.c_uint

reader = lib.archive_read_new()
entry = c.c_void_p()

def checked(result):
    if result != 0:
        message = lib.archive_error_string(reader)
        raise RuntimeError(message.decode() if message else 'libarchive extraction failed')

try:
    checked(lib.archive_read_support_filter_all(reader))
    checked(lib.archive_read_support_format_7zip(reader))
    checked(lib.archive_read_open_filename(reader, os.fsencode(archive_name), 1024 * 1024))
    while True:
        result = lib.archive_read_next_header(reader, c.byref(entry))
        if result == 1:
            break
        checked(result)
        original = PurePosixPath(os.fsdecode(lib.archive_entry_pathname(entry)))
        if original.is_absolute() or '..' in original.parts:
            raise RuntimeError('Unsafe archive path: ' + str(original))
        parts = original.parts
        kind = lib.archive_entry_filetype(entry)
        if parts[:len(strip_parts)] != strip_parts:
            if kind == stat.S_IFDIR and strip_parts[:len(parts)] == parts:
                continue
            raise RuntimeError('Unexpected Qt archive prefix: ' + str(original))
        relative = parts[len(strip_parts):]
        if not relative:
            continue
        target = prefix.joinpath(*relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        if os.path.commonpath((str(prefix), str(target.parent.resolve()))) != str(prefix):
            raise RuntimeError('Archive path escapes SDK: ' + str(original))
        mode = lib.archive_entry_perm(entry) & 0o777
        if kind == stat.S_IFDIR:
            target.mkdir(exist_ok=True)
        elif kind == stat.S_IFREG:
            if target.is_symlink():
                raise RuntimeError('Archive file replaces symlink: ' + str(original))
            with target.open('wb') as stream:
                checked(lib.archive_read_data_into_fd(reader, stream.fileno()))
            target.chmod(mode)
        elif kind == stat.S_IFLNK:
            link = os.fsdecode(lib.archive_entry_symlink(entry))
            resolved = (target.parent / link).resolve()
            if os.path.isabs(link) or os.path.commonpath((str(prefix), str(resolved))) != str(prefix):
                raise RuntimeError('Archive symlink escapes SDK: ' + str(original))
            if target.is_symlink() and os.readlink(target) == link:
                continue
            target.symlink_to(link)
        else:
            raise RuntimeError('Unsupported archive entry type: ' + str(original))
finally:
    lib.archive_read_free(reader)
PY
}

extract_source() {
    mkdir -p "$SDK_WORKDIR/src/$1"
    tar -xf "$FETCHED_ARCHIVE" -C "$SDK_WORKDIR/src/$1" --strip-components=1
}

copy_licenses() {
    local name=$1 source_dir=$2 license relative
    while IFS= read -r -d '' license; do
        relative=${license#"$source_dir/"}
        mkdir -p "$metadata/licenses/$name/$(dirname "$relative")"
        cp "$license" "$metadata/licenses/$name/$relative"
    done < <(find "$source_dir" -type f \( -iname 'license*' -o -iname 'copying*' -o -iname 'copyright*' -o -path '*/LICENSES/*' \) -print0)
}

QT_VERSION=6.4.3
QT_ARCHIVE_VERSION=6.4.3-0-202303121118
QT_REPOSITORY=https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_643
QT_PLATFORM=Linux-RHEL_8_4-GCC-Linux-RHEL_8_4-X86_64

# Official packages were built on RHEL 8.4. Their TLS plugin uses OpenSSL
# 1.1; ICU 56 is a private dependency of this exact official Qt build.
fetch_archive qtbase-binary \
    "$QT_REPOSITORY/qt.qt6.643.gcc_64/${QT_ARCHIVE_VERSION}qtbase-$QT_PLATFORM.7z" \
    e55568234e2b39951300fae72683cc8dd657130c7256bc4fab88b75d386e32be qtbase.7z
extract_qt_archive "$QT_VERSION/gcc_64"
fetch_archive qtdeclarative-binary \
    "$QT_REPOSITORY/qt.qt6.643.gcc_64/${QT_ARCHIVE_VERSION}qtdeclarative-$QT_PLATFORM.7z" \
    ce45109aa5e4f3e32cbf56ed8c3f2f05a5b08a9eeda24e7ce306976e5f87b593 qtdeclarative.7z
extract_qt_archive "$QT_VERSION/gcc_64"
fetch_archive qtshadertools-binary \
    "$QT_REPOSITORY/qt.qt6.643.qtshadertools.gcc_64/${QT_ARCHIVE_VERSION}qtshadertools-$QT_PLATFORM.7z" \
    d73c6325b93a281645532dee5a714866cde17ecb5800ea446fd04cc3f109158e qtshadertools.7z
extract_qt_archive "$QT_VERSION/gcc_64"
fetch_archive icu-binary \
    "$QT_REPOSITORY/qt.qt6.643.gcc_64/${QT_ARCHIVE_VERSION}icu-linux-Rhel7.2-x64.7z" \
    2f531c71b330e756c5475548378523450cdf4f6e6175bce4df864abfe7b336ea icu.7z
extract_qt_archive "$QT_VERSION/gcc_64"
printf '[Paths]\nPrefix=..\n' > "$SDK_PREFIX/bin/qt.conf"
printf '[Paths]\nPrefix=..\n' > "$SDK_PREFIX/libexec/qt.conf"

# Keep the maintained, matching RHEL library/header pair entirely in the SDK.
mkdir -p "$SDK_PREFIX/lib/engines-1.1"
cp -a /usr/include/openssl "$SDK_PREFIX/include/"
for library in libssl libcrypto; do
    library_path=$(readlink -f "/usr/lib64/$library.so.1.1")
    [[ -f "$library_path" ]] || { echo "Missing OpenSSL DSO: $library" >&2; exit 1; }
    cp "$library_path" "$SDK_PREFIX/lib/$library.so.1.1"
    ln -s "$library.so.1.1" "$SDK_PREFIX/lib/$library.so"
done
cp -a /usr/lib64/engines-1.1/. "$SDK_PREFIX/lib/engines-1.1/"
mkdir -p "$SDK_PREFIX/lib/pkgconfig"
for package in openssl libssl libcrypto; do
    sed -e "s|^prefix=.*|prefix=$SDK_PREFIX|" \
        -e 's|^libdir=.*|libdir=${prefix}/lib|' \
        -e 's|^includedir=.*|includedir=${prefix}/include|' \
        "/usr/lib64/pkgconfig/$package.pc" > "$SDK_PREFIX/lib/pkgconfig/$package.pc"
done

fetch_archive libical-source \
    https://github.com/libical/libical/releases/download/v3.0.20/libical-3.0.20.tar.gz \
    e73de92f5a6ce84c1b00306446b290a2b08cdf0a80988eca0a2c9d5c3510b4c2 libical.tar.gz
extract_source libical
fetch_archive extra-cmake-modules-source \
    https://download.kde.org/stable/frameworks/5.103/extra-cmake-modules-5.103.0.tar.xz \
    92ca2e55cb38956fbdeaf254231f074647173ccfd12dc9664989c6fa9e9c4346 extra-cmake-modules.tar.xz
extract_source extra-cmake-modules
fetch_archive fcitx-qt5-source \
    https://download.fcitx-im.org/fcitx-qt5/fcitx-qt5-1.2.7.tar.xz \
    951fcf8f1db23ed22ad91094eb4c6c906f92005a3643b52f666bd8a331163147 fcitx-qt5.tar.xz
extract_source fcitx-qt5
fetch_archive patchelf-source \
    https://github.com/NixOS/patchelf/releases/download/0.18.0/patchelf-0.18.0.tar.bz2 \
    1952b2a782ba576279c211ee942e341748fdb44997f704dd53def46cd055470b patchelf.tar.bz2
extract_source patchelf

build_cmake_project() {
    local name=$1
    shift
    cmake -S "$SDK_WORKDIR/src/$name" -B "$SDK_WORKDIR/build/$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib "$@"
    cmake --build "$SDK_WORKDIR/build/$name" --parallel "$SDK_JOBS"
    cmake --install "$SDK_WORKDIR/build/$name"
}

# RHEL's libical 3.0.3 links ICU 60. Build this small library without RSCALE
# to avoid bringing a second ICU into the private runtime.
build_cmake_project libical \
    -DSHARED_ONLY=ON -DWITH_CXX_BINDINGS=OFF -DICAL_GLIB=OFF \
    -DICAL_BUILD_DOCS=OFF -DLIBICAL_BUILD_TESTING=OFF -DUSE_BUILTIN_TZDATA=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_ICU=ON
build_cmake_project extra-cmake-modules \
    -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF
build_cmake_project fcitx-qt5 \
    -DENABLE_QT5=OFF -DENABLE_QT6=ON -DENABLE_LIBRARY=OFF \
    -DCMAKE_INSTALL_QT6PLUGINDIR="$SDK_PREFIX/plugins"
(
    mkdir -p "$SDK_WORKDIR/build/patchelf"
    cd "$SDK_WORKDIR/build/patchelf"
    "$SDK_WORKDIR/src/patchelf/configure" --prefix="$SDK_PREFIX"
    make -j"$SDK_JOBS"
    make install
)

printf 'library\tcompiler_path\n' > "$metadata/compiler-libraries.tsv"
for library in libstdc++.so.6 libgcc_s.so.1; do
    library_path=$(g++ -print-file-name="$library")
    [[ "$library_path" == /* && -f "$library_path" ]] || {
        echo "Compiler did not resolve $library to a runtime DSO." >&2; exit 1;
    }
    cp -L "$library_path" "$SDK_PREFIX/lib/$library"
    printf '%s\t%s\n' "$library" "$(readlink -f "$library_path")" >> "$metadata/compiler-libraries.tsv"
done

# Extract notices from matching upstream sources, without compiling Qt or
# retaining its large source trees in the image.
qt_source_licenses() {
    python3 - "$FETCHED_ARCHIVE" "$metadata/licenses/$1" <<'PY'
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile

destination = Path(sys.argv[2])
with tarfile.open(sys.argv[1]) as archive:
    for member in archive:
        path = PurePosixPath(member.name)
        if not member.isfile() or not (path.name.lower().startswith(('license', 'copying', 'copyright')) or 'LICENSES' in path.parts):
            continue
        if path.is_absolute() or '..' in path.parts:
            raise RuntimeError('Unsafe source license path: ' + member.name)
        target = destination.joinpath(*path.parts[1:])
        target.parent.mkdir(parents=True, exist_ok=True)
        with archive.extractfile(member) as source, target.open('wb') as output:
            shutil.copyfileobj(source, output)
PY
}

fetch_archive qtbase-source \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtbase-everywhere-src-$QT_VERSION.tar.xz" \
    5087c9e5b0165e7bc3c1a4ab176b35d0cd8f52636aea903fa377bdba00891a60 qtbase-source.tar.xz
qt_source_licenses qtbase
fetch_archive qtdeclarative-source \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtdeclarative-everywhere-src-$QT_VERSION.tar.xz" \
    67ded1b3b988135e3600916b34bb156fe3986c88420c6f87a6d0fd53521ba795 qtdeclarative-source.tar.xz
qt_source_licenses qtdeclarative
fetch_archive qtshadertools-source \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtshadertools-everywhere-src-$QT_VERSION.tar.xz" \
    c648b74ce3e769a20bd62792a589c540bceacfedf2f38fc7273d2c39782768f8 qtshadertools-source.tar.xz
qt_source_licenses qtshadertools
fetch_archive icu-license \
    https://raw.githubusercontent.com/unicode-org/icu/24c90c0693a0ba7aea00986c34676b55f766cfac/icu4c/license.html \
    a4fd81c0bb0aee44d90376562f53c62d17ce252b93190251d172ec837ee5c6c8 icu-license.html
mkdir -p "$metadata/licenses/icu"
cp "$FETCHED_ARCHIVE" "$metadata/licenses/icu/license.html"
for component in libical extra-cmake-modules fcitx-qt5 patchelf; do
    copy_licenses "$component" "$SDK_WORKDIR/src/$component"
done
mkdir -p "$metadata/licenses/system-runtime"
cp -a /usr/share/licenses/. "$metadata/licenses/system-runtime/"
rpm -qa --qf '%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\n' | sort > "$metadata/build-packages.tsv"
rpm -qa --qf '%{NAME}\t%{SOURCERPM}\t%{VENDOR}\n' | sort > "$metadata/rpm-sources.tsv"
printf '%s\n' \
    'Qt 6.4.3: official Linux-RHEL_8_4-GCC-Linux-RHEL_8_4-X86_64 SDK, installer build 202303121118' \
    'ICU 56.1 source: https://github.com/unicode-org/icu/tree/24c90c0693a0ba7aea00986c34676b55f766cfac' \
    'fcitx-qt5 1.2.7: upstream tag commit 3d21a1c8b4e286dcfd955662366858ddf4823d02' \
    'OpenSSL 1.1: maintained Rocky Linux 8 RPMs, exact versions in build-packages.tsv and source RPMs in rpm-sources.tsv' \
    > "$metadata/source-revisions"
printf '%s\n' "$build_id" > "$metadata/complete"
echo "AMD64 runtime SDK complete: $SDK_PREFIX"
