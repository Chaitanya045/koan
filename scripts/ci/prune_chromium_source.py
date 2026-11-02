#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Deletes what a Chromium build of `chrome` doesn't need from a checkout.

    prune_chromium_source.py <chromium/src>

Used by the macOS CI (.github/workflows/macos.yml) to keep the source
archive that every build job downloads small: test data, web tests,
benchmarks and other platforms' code, about a third of the checkout.

The lists are the ones Chromium's own "lite" source tarball is made with
(chromium-<version>-lite.tar.xz, which distributions build from): the
publish_tarball recipe and its export_tarball.py in
https://chromium.googlesource.com/chromium/tools/build, recipes/. Koan
keeps the toolchains that recipe also drops (llvm-build, rust-toolchain,
node), because Koan builds with Chromium's own toolchain. Works with
Python 3.9.
"""

import os
import re
import shutil
import sys

# export_tarball.py: nonessential_dirs and TEST_DIRS. Like the tarball,
# their GN, GRIT and .pydeps files stay, so gn gen still loads.
REMOVE = (
    'buildtools/reclient_cfgs/reproxy.cfg',
    'third_party/blink/tools',
    'third_party/blink/web_tests',
    'third_party/devtools-frontend/src/third_party/rollup_libs',
    'third_party/hunspell_dictionaries',
    'third_party/hunspell/tests',
    'third_party/jdk/current',
    'third_party/jdk/extras',
    'third_party/liblouis/src/tests/braille-specs',
    'third_party/xdg-utils/tests',
    'v8/test',
    'android_webview/tools/cts_archive/cipd',
    'base/tracing/test/data',
    'chrome/test/data',
    'components/test/data',
    'content/test/data/accessibility',
    'content/test/data/gpu',
    'content/test/data/media',
    'courgette/testdata',
    'extensions/test/data',
    'media/test/data',
    'native_client/src/trusted/service_runtime/testdata',
    'testing/libfuzzer/fuzzers/wasm_corpus',
    'third_party/blink/manual_tests',
    'third_party/blink/perf_tests',
    'third_party/boringssl/src/third_party/wycheproof_testvectors',
    'third_party/breakpad/breakpad/src/processor/testdata',
    'third_party/catapult/tracing/test_data',
    'third_party/dawn/test',
    'third_party/devtools-frontend/src/front_end/panels/timeline/fixtures',
    'third_party/expat/src/testdata',
    'third_party/glslang/src/Test',
    'third_party/harfbuzz-ng/src/test',
    'third_party/libc++/src/test/std',
    'third_party/openh264/src/res',
    'third_party/opus/tests/resources',
    'third_party/ots/src/tests/fonts',
    'third_party/robolectric/cipd',
    'third_party/rust-src/src/gcc/gcc/testsuite',
    'third_party/rust-src/src/llvm-project/clang/test',
    'third_party/rust-src/src/llvm-project/llvm/test',
    'third_party/screen-ai/linux/resources',
    'third_party/sqlite/fuzz',
    'third_party/sqlite/src/test',
    'third_party/swiftshader/tests/regres',
    'third_party/test_fonts/test_fonts',
    'tools/perf/testdata',
    # publish_tarball.py: purge_directories, without the toolchains.
    'buildtools/reclient',
    'third_party/angle/third_party/VK-GL-CTS',
    'third_party/catapult/third_party/vinn/third_party/v8',
    'third_party/dawn/third_party/khronos/OpenGL-Registry/specs',
    'third_party/dawn/tools/golang',
    'third_party/jetstream',
    'third_party/llvm-build-tools',
    'third_party/rust-src',
    'third_party/speedometer',
    'third_party/webgl',
    'tools/skia_goldctl',
)

# export_tarball.py: ESSENTIAL_FILES, kept inside the directories above.
KEEP = (
    'chrome/test/data/webui/i18n_process_css_test.html',
    'chrome/test/data/webui/mojo/foobar.mojom',
    # gn gen reads it.
    'v8/test/torque/test-torque.tq',
)

# publish_tarball.py: prune_directories. These also keep their license
# files.
PRUNE = (
    'android_webview',
    'chrome/android',
    'chromecast',
    'ios',
    'third_party/android_platform',
    'third_party/closure_compiler',
    'third_party/instrumented_libs',
    'third_party/libphonenumber/dist/resources/metadata',
)
BUILD_FILE = re.compile(r'\.(gn|gni|grd|grdp|isolate|pydeps)(\.\S+)?$')
LICENSE = re.compile(r'.*(copying|copyright|license)[^/]*$', re.I)
NOT_LICENSE = re.compile(
    r'.*\.(cc|cfg|cpp|h|java|js|json|m|patch|pl|py|rs|sh|sha1|stderr|ts|'
    r'ya?ml)$', re.I)


def keep_build_file(full):
    """GN, GRIT and .pydeps files, which gn gen reads even for targets that
    aren't built (crbug.com/1362021)."""
    return BUILD_FILE.search(os.path.basename(full)) is not None


def remove_tree(src, rel, licenses):
    """Deletes rel's files except build files (and, with licenses, the
    license texts) and the KEEP files. Returns whether rel existed."""
    path = os.path.join(src, rel)
    if not os.path.lexists(path):
        return 0
    if os.path.isfile(path) or os.path.islink(path):
        os.remove(path)
        return 1
    for dirpath, dirnames, filenames in os.walk(path):
        for name in filenames:
            full = os.path.join(dirpath, name)
            if keep_build_file(full) or os.path.relpath(full, src) in KEEP:
                continue
            if licenses and LICENSE.match(full) and not NOT_LICENSE.match(full):
                continue
            os.remove(full)
        # Symlinks to directories are listed as directories; remove them
        # like files.
        for name in list(dirnames):
            full = os.path.join(dirpath, name)
            if os.path.islink(full):
                os.remove(full)
                dirnames.remove(name)
    return 1


def remove_empty_dirs(src, rel):
    path = os.path.join(src, rel)
    if not os.path.isdir(path) or os.path.islink(path):
        return
    for dirpath, dirnames, filenames in os.walk(path, topdown=False):
        if dirpath != path and not os.listdir(dirpath):
            os.rmdir(dirpath)


def main(argv):
    if len(argv) != 2 or not os.path.isfile(os.path.join(argv[1], 'DEPS')):
        sys.stderr.write('usage: prune_chromium_source.py <chromium/src>\n')
        return 2
    src = os.path.abspath(argv[1])
    removed = sum(remove_tree(src, rel, False) for rel in REMOVE)
    pruned = sum(remove_tree(src, rel, True) for rel in PRUNE)
    for rel in REMOVE + PRUNE:
        remove_empty_dirs(src, rel)
    print('removed %d paths, pruned %d directories' % (removed, pruned))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
