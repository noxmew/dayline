#!/usr/bin/env bash
set -Eeuo pipefail

trap 'printf "Runtime SDK failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR

# This script installs build dependencies. Refuse to run on the host or a
# different distribution, where doing so could change the intended ABI floor.
[[ -f /.dockerenv ]] || { echo 'Run this script inside the Rocky Linux 8 build container.' >&2; exit 1; }
source /etc/os-release
[[ "$ID" == rocky && "$VERSION_ID" == 8.* && "$EUID" == 0 ]] || {
    echo 'The runtime SDK requires a root Rocky Linux 8 container.' >&2; exit 1;
}
[[ "$(getconf GNU_LIBC_VERSION)" == 'glibc 2.28' ]] || {
    echo 'The runtime SDK must be built against glibc 2.28.' >&2; exit 1;
}
case "$(uname -m)" in
    x86_64) qt_graphics_options=(-opengl desktop) ;;
    aarch64) qt_graphics_options=(-opengl es2 -egl -feature-xcb-egl-plugin) ;;
    *) echo 'Only native amd64 and arm64 builds are supported.' >&2; exit 1 ;;
esac

SDK_PREFIX=${SDK_PREFIX:-/sdk/prefix}
SDK_WORKDIR=${SDK_WORKDIR:-/sdk/runtime-build}
SDK_JOBS=${SDK_JOBS:-$(nproc)}
[[ "$SDK_PREFIX" == /* && "$SDK_WORKDIR" == /* && "$SDK_PREFIX" != / && "$SDK_WORKDIR" != / ]] || {
    echo 'SDK_PREFIX and SDK_WORKDIR must be non-root absolute paths.' >&2; exit 1;
}
[[ "$SDK_JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'SDK_JOBS must be a positive integer.' >&2; exit 1; }

# The pinned image predates current source-host certificate chains. Refresh
# distribution trust roots before fetching sources; TLS verification stays on.
dnf -y upgrade ca-certificates
update-ca-trust
dnf -y --setopt=install_weak_deps=False --enablerepo=powertools install \
    gcc-toolset-12-gcc-c++ cmake ninja-build make perl python3 pkgconf-pkg-config \
    curl tar xz bzip2 gzip patch file binutils findutils which \
    libxcb-devel xcb-util-image-devel xcb-util-keysyms-devel \
    xcb-util-renderutil-devel xcb-util-wm-devel libxkbcommon-x11-devel \
    mesa-libGL-devel mesa-libEGL-devel libglvnd-devel fontconfig-devel freetype-devel \
    dbus-devel zlib-devel libX11-devel libX11-xcb libXext-devel \
    libXrender-devel libXi-devel libSM-devel libICE-devel libxkbcommon-devel \
    libXfixes-devel libXrandr-devel libXcursor-devel libXinerama-devel \
    bison flex gperf expat-devel
dnf clean all

# The vendor SCL enable script reads optional shell variables without defaults.
set +u
source /opt/rh/gcc-toolset-12/enable
set -u
export PATH="$SDK_PREFIX/bin:$PATH"
export CMAKE_PREFIX_PATH="$SDK_PREFIX"
export PKG_CONFIG_PATH="$SDK_PREFIX/lib/pkgconfig"
export LD_LIBRARY_PATH="$SDK_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

metadata="$SDK_PREFIX/share/dayline-runtime"
build_id=$(printf '%s\n%s\n%s\n' "$(sha256sum "${BASH_SOURCE[0]}" | cut -d ' ' -f 1)" \
    "$(uname -m)" "$SDK_PREFIX" | sha256sum | cut -d ' ' -f 1)
if [[ -f "$metadata/build-id" ]]; then
    [[ "$(cat "$metadata/build-id")" == "$build_id" ]] || {
        echo 'SDK cache configuration differs; use an empty SDK_PREFIX.' >&2; exit 1;
    }
elif [[ -d "$SDK_PREFIX" && -n "$(find "$SDK_PREFIX" -mindepth 1 -print -quit)" ]]; then
    echo 'SDK_PREFIX already contains an unrecognized SDK; use an empty directory.' >&2; exit 1
fi
mkdir -p "$metadata/licenses" "$metadata/stages" "$SDK_WORKDIR/downloads" "$SDK_WORKDIR/src" "$SDK_WORKDIR/build"
printf '%s\n' "$build_id" > "$metadata/build-id"
if [[ -f "$metadata/complete" ]]; then
    echo "Runtime SDK already complete: $SDK_PREFIX"
    exit 0
fi

QT_VERSION=6.4.3
OPENSSL_VERSION=3.5.9
LIBICAL_VERSION=3.0.20
ECM_VERSION=5.103.0
FCITX_VERSION=1.2.7
PATCHELF_VERSION=0.18.0

fetch_source() {
    local name=$1 url=$2 checksum=$3 filename=$4 destination=$5
    local archive="$SDK_WORKDIR/downloads/$filename"
    if [[ ! -f "$archive" ]]; then
        curl --fail --show-error --location --retry 3 --output "$archive.part" "$url"
        mv "$archive.part" "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check --status
    if [[ -f "$destination/.dayline-extracted" ]]; then
        [[ "$(cat "$destination/.dayline-extracted")" == "$checksum" ]] || {
            echo "Source directory differs from pinned archive: $name" >&2; exit 1;
        }
    else
        mkdir -p "$destination"
        tar -xf "$archive" -C "$destination" --strip-components=1
        printf '%s\n' "$checksum" > "$destination/.dayline-extracted"
    fi
    printf '%s\t%s\t%s\n' "$name" "$checksum" "$url" >> "$metadata/sources.tsv"
}

copy_licenses() {
    local name=$1 source_dir=$2 license relative
    while IFS= read -r -d '' license; do
        relative=${license#"$source_dir/"}
        mkdir -p "$metadata/licenses/$name/$(dirname "$relative")"
        cp "$license" "$metadata/licenses/$name/$relative"
    done < <(find "$source_dir" -type f \( -iname 'license*' -o -iname 'copying*' -o -iname 'copyright*' \) -print0)
}

run_stage() {
    local name=$1
    shift
    if [[ ! -f "$metadata/stages/$name" ]]; then
        echo "Building runtime SDK: $name"
        "$@"
        touch "$metadata/stages/$name"
    fi
}

printf 'component\tsha256\turl\n' > "$metadata/sources.tsv"
fetch_source qtbase \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtbase-everywhere-src-$QT_VERSION.tar.xz" \
    5087c9e5b0165e7bc3c1a4ab176b35d0cd8f52636aea903fa377bdba00891a60 \
    "qtbase-$QT_VERSION.tar.xz" "$SDK_WORKDIR/src/qtbase"
fetch_source qtshadertools \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtshadertools-everywhere-src-$QT_VERSION.tar.xz" \
    c648b74ce3e769a20bd62792a589c540bceacfedf2f38fc7273d2c39782768f8 \
    "qtshadertools-$QT_VERSION.tar.xz" "$SDK_WORKDIR/src/qtshadertools"
fetch_source qtdeclarative \
    "https://download.qt.io/archive/qt/6.4/$QT_VERSION/submodules/qtdeclarative-everywhere-src-$QT_VERSION.tar.xz" \
    67ded1b3b988135e3600916b34bb156fe3986c88420c6f87a6d0fd53521ba795 \
    "qtdeclarative-$QT_VERSION.tar.xz" "$SDK_WORKDIR/src/qtdeclarative"
fetch_source openssl \
    "https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VERSION/openssl-$OPENSSL_VERSION.tar.gz" \
    603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a \
    "openssl-$OPENSSL_VERSION.tar.gz" "$SDK_WORKDIR/src/openssl"
fetch_source libical \
    "https://github.com/libical/libical/releases/download/v$LIBICAL_VERSION/libical-$LIBICAL_VERSION.tar.gz" \
    e73de92f5a6ce84c1b00306446b290a2b08cdf0a80988eca0a2c9d5c3510b4c2 \
    "libical-$LIBICAL_VERSION.tar.gz" "$SDK_WORKDIR/src/libical"
fetch_source extra-cmake-modules \
    "https://download.kde.org/stable/frameworks/5.103/extra-cmake-modules-$ECM_VERSION.tar.xz" \
    92ca2e55cb38956fbdeaf254231f074647173ccfd12dc9664989c6fa9e9c4346 \
    "extra-cmake-modules-$ECM_VERSION.tar.xz" "$SDK_WORKDIR/src/extra-cmake-modules"
fetch_source fcitx-qt5 \
    "https://download.fcitx-im.org/fcitx-qt5/fcitx-qt5-$FCITX_VERSION.tar.xz" \
    951fcf8f1db23ed22ad91094eb4c6c906f92005a3643b52f666bd8a331163147 \
    "fcitx-qt5-$FCITX_VERSION.tar.xz" "$SDK_WORKDIR/src/fcitx-qt5"
fetch_source patchelf \
    "https://github.com/NixOS/patchelf/releases/download/$PATCHELF_VERSION/patchelf-$PATCHELF_VERSION.tar.bz2" \
    1952b2a782ba576279c211ee942e341748fdb44997f704dd53def46cd055470b \
    "patchelf-$PATCHELF_VERSION.tar.bz2" "$SDK_WORKDIR/src/patchelf"

build_openssl() (
    mkdir -p "$SDK_WORKDIR/build/openssl"
    cd "$SDK_WORKDIR/build/openssl"
    # Provider/engine paths belong to the final private installation. Stage the
    # installation, then relocate build-only pkg-config metadata into the SDK.
    "$SDK_WORKDIR/src/openssl/Configure" shared no-tests \
        --prefix=/usr/lib/dayline --libdir=lib --openssldir=/etc/ssl
    make -j"$SDK_JOBS"
    make DESTDIR="$SDK_WORKDIR/build/openssl-stage" install_sw
    cp -a "$SDK_WORKDIR/build/openssl-stage/usr/lib/dayline/." "$SDK_PREFIX/"
    sed -i "s|^prefix=/usr/lib/dayline$|prefix=$SDK_PREFIX|" "$SDK_PREFIX"/lib/pkgconfig/{openssl,libssl,libcrypto}.pc
)

build_cmake_project() {
    local name=$1
    shift
    cmake -S "$SDK_WORKDIR/src/$name" -B "$SDK_WORKDIR/build/$name" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib "$@"
    cmake --build "$SDK_WORKDIR/build/$name" --parallel "$SDK_JOBS"
    cmake --install "$SDK_WORKDIR/build/$name"
}

build_qtbase() (
    mkdir -p "$SDK_WORKDIR/build/qtbase"
    cd "$SDK_WORKDIR/build/qtbase"
    "$SDK_WORKDIR/src/qtbase/configure" \
        -prefix "$SDK_PREFIX" -libdir lib -opensource -confirm-license \
        -release -shared -nomake examples -nomake tests -no-widgets -no-icu \
        -fontconfig -system-freetype -qt-harfbuzz -qt-pcre -qt-libpng -qt-libjpeg \
        -openssl-linked -sql-sqlite -qt-sqlite -xcb "${qt_graphics_options[@]}" \
        -no-feature-cups -no-feature-vulkan -no-feature-eglfs -no-feature-linuxfb \
        -- -DOPENSSL_ROOT_DIR="$SDK_PREFIX" -DOpenGL_GL_PREFERENCE=LEGACY \
        -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    cmake --build . --parallel "$SDK_JOBS"
    cmake --install .
)

build_patchelf() (
    mkdir -p "$SDK_WORKDIR/build/patchelf"
    cd "$SDK_WORKDIR/build/patchelf"
    "$SDK_WORKDIR/src/patchelf/configure" --prefix="$SDK_PREFIX"
    make -j"$SDK_JOBS"
    make install
)

run_stage openssl build_openssl
run_stage libical build_cmake_project libical \
    -DSHARED_ONLY=ON -DWITH_CXX_BINDINGS=OFF -DICAL_GLIB=OFF \
    -DICAL_BUILD_DOCS=OFF -DLIBICAL_BUILD_TESTING=OFF -DUSE_BUILTIN_TZDATA=OFF
run_stage qtbase build_qtbase
run_stage qtshadertools build_cmake_project qtshadertools \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
run_stage qtdeclarative build_cmake_project qtdeclarative \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DFEATURE_quickcontrols2_basic=ON \
    -DFEATURE_quickcontrols2_fusion=OFF -DFEATURE_quickcontrols2_imagine=OFF \
    -DFEATURE_quickcontrols2_material=OFF -DFEATURE_quickcontrols2_universal=OFF \
    -DFEATURE_quickcontrols2_macos=OFF -DFEATURE_quickcontrols2_ios=OFF \
    -DFEATURE_quickcontrols2_windows=OFF -DFEATURE_quickdialogs2=OFF
run_stage extra-cmake-modules build_cmake_project extra-cmake-modules \
    -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF
run_stage fcitx-qt5 build_cmake_project fcitx-qt5 \
    -DENABLE_QT5=OFF -DENABLE_QT6=ON -DENABLE_LIBRARY=OFF
run_stage patchelf build_patchelf

# GCC Toolset may implement newer C++ APIs with libstdc++_nonshared.a while
# retaining the distribution runtime SONAME. Copy the actual selected DSOs.
printf 'library\tcompiler_path\n' > "$metadata/compiler-libraries.tsv"
for library in libstdc++.so.6 libgcc_s.so.1; do
    library_path=$(g++ -print-file-name="$library")
    [[ "$library_path" == /* && -f "$library_path" ]] || {
        echo "Compiler did not resolve $library to a runtime DSO." >&2; exit 1;
    }
    cp -L "$library_path" "$SDK_PREFIX/lib/$library"
    printf '%s\t%s\n' "$library" "$(readlink -f "$library_path")" >> "$metadata/compiler-libraries.tsv"
done

for component in qtbase qtshadertools qtdeclarative openssl libical fcitx-qt5; do
    copy_licenses "$component" "$SDK_WORKDIR/src/$component"
done
mkdir -p "$metadata/licenses/system-runtime"
cp -a /usr/share/licenses/. "$metadata/licenses/system-runtime/"
rpm -qa --qf '%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\n' | sort > "$metadata/build-packages.tsv"
printf '%s\n' 'fcitx-qt5 1.2.7: upstream tag commit 3d21a1c8b4e286dcfd955662366858ddf4823d02' > "$metadata/source-revisions"
printf '%s\n' "$build_id" > "$metadata/complete"
echo "Runtime SDK complete: $SDK_PREFIX"
