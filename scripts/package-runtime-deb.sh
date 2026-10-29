#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 4 ]] || { echo 'Usage: package-runtime-deb.sh STAGE VERSION ARCH OUTPUT' >&2; exit 1; }
stage=$(realpath "$1")
version=$2
arch=$3
output=$4
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
[[ "$arch" == amd64 || "$arch" == arm64 ]]
[[ $(dpkg --print-architecture) == "$arch" ]]
[[ -x "$stage/usr/bin/dayline" && -x "$stage/usr/lib/dayline/dayline" ]]
[[ -f "$stage/usr/share/dayline/runtime-dependencies" ]]
[[ -d "$stage/usr/share/doc/dayline/runtime/licenses" ]]
mkdir -p "$stage/DEBIAN" "$output"
dependencies=$(cat "$stage/usr/share/dayline/runtime-dependencies")
[[ "$dependencies" == *'libc6 (>= 2.28)'* ]]
cat > "$stage/DEBIAN/control" <<EOF
Package: dayline
Version: $version
Architecture: $arch
Maintainer: Dayline
Section: utils
Priority: optional
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Depends: $dependencies
Homepage: https://github.com/noxmew/dayline
Description: Dayline native desktop calendar
 Desktop calendar with a private Qt runtime built on glibc 2.28.
EOF
find "$stage" -type d -exec chmod 755 {} +
package="$output/dayline_${version}_${arch}.deb"
# xz is supported by the UOS 20 installer; newer dpkg defaults to zstd.
dpkg-deb --root-owner-group -Zxz -z6 --build "$stage" "$package"
