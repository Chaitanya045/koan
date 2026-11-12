#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Stands in for Google's Widevine signer in Koan's build.

With `enable_cdm_host_verification = true` (build/koan-mac.gn), Chromium's
//chrome:sign_chrome_framework_for_widevine runs
//third_party/widevine/scripts/signature_generator.py, which lives in Google's
internal checkout only. Koan's patch to third_party/widevine/cdm/widevine.gni
points the action here instead. It writes an empty `Koan Framework.sig`, the
file the build then copies into the framework's Resources.

An empty signature makes the Widevine CDM report the host as unverified
(no VMP). It still loads and plays content that doesn't require VMP.
scripts/mac/widevine_vmp_sign.sh replaces the file with a real VMP signature
after the build, before the app is code-signed.

Takes the real signer's arguments (--input_file, --output_file, --flags,
--ignore_missing_cert) and ignores all but --output_file.
"""

import argparse
import sys


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument('--input_file', required=True)
  parser.add_argument('--output_file', required=True)
  parser.add_argument('--flags', default='0')
  parser.add_argument('--ignore_missing_cert', action='store_true')
  args = parser.parse_args(argv)
  with open(args.output_file, 'wb'):
    pass
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
