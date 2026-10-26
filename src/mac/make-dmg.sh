#!/bin/sh
# Turns the built Muralop.app into a self-contained, ad-hoc signed disk image.
# Run by `ninja dmg`:
#   make-dmg.sh <Muralop.app> <output.dmg> <directory holding libmpv.dylib>
#
# libmpv and everything it links (Homebrew's ffmpeg, libass, libplacebo, ...) are copied
# into Contents/Frameworks by dylibbundler, and the bundle's minimum macOS version is
# raised to the newest one those libraries were built for.
set -eu

app=$1
dmg=$2
libdir=$3

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/root"
bundle="$work/root/$(basename "$app")"
cp -R "$app" "$bundle"
exe="$bundle/Contents/MacOS/Muralop"
frameworks="$bundle/Contents/Frameworks"

dylibbundler --create-dir --overwrite-files --bundle-deps --fix-file "$exe" \
    --dest-dir "$frameworks" --install-path @executable_path/../Frameworks/ \
    --search-path "$libdir" < /dev/null
chmod -R u+w "$frameworks"

# dylibbundler leaves LC_RPATH entries behind, sometimes the same one twice, and dyld
# refuses to load a library with a duplicate. Nothing refers to @rpath any more (the
# check below makes sure), so drop them all.
for f in "$exe" "$frameworks"/*; do
    while rpath=$(otool -l "$f" | awk '$1 == "cmd" { r = ($2 == "LC_RPATH") } r && $1 == "path" { print $2; exit }') &&
        [ -n "$rpath" ]; do
        install_name_tool -delete_rpath "$rpath" "$f"
    done
done

# Everything must now load from the bundle or from the OS.
outside=$(for f in "$exe" "$frameworks"/*; do otool -L "$f" | tail -n +2; done |
    awk '{ print $1 }' | grep -v -e '^/usr/lib/' -e '^/System/Library/' -e '^@executable_path/\.\./Frameworks/' | sort -u || true)
if [ -n "$outside" ]; then
    echo "make-dmg: libraries still referenced from outside the bundle:" >&2
    echo "$outside" >&2
    exit 1
fi

minos=$(for f in "$exe" "$frameworks"/*; do otool -l "$f"; done |
    awk '$1 == "cmd" { build = ($2 == "LC_BUILD_VERSION") } build && $1 == "minos" { print $2 }' |
    sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)
/usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion $minos" "$bundle/Contents/Info.plist"
echo "make-dmg: $(find "$frameworks" -type f | wc -l | tr -d ' ') libraries bundled, needs macOS $minos or later"

# Ad-hoc signatures: without one Apple Silicon refuses to run the code at all.
codesign --force --sign - "$frameworks"/*
codesign --force --sign - "$bundle"
codesign --verify --strict --deep "$bundle"

ln -s /Applications "$work/root/Applications"
rm -f "$dmg"
# hdiutil now and then fails with "Resource busy" on CI machines; try again.
for _ in 1 2 3; do
    if hdiutil create -volname "Muralop" -srcfolder "$work/root" -fs HFS+ -format UDZO "$dmg"; then
        exit 0
    fi
    sleep 5
done
exit 1
