#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""The signed update manifest of a release (koan-update.json).

Each tagged release carries koan-update.json. The app fetches
https://github.com/koan-browser/koan/releases/latest/download/koan-update.json,
downloads the disk image it names, and installs it only if the signature
verifies against the public key built into the app
(src/browser/updates/koan_update_public_key.txt). The private key is the
KOAN_UPDATE_SIGNING_KEY secret of the `release` environment.

  update_manifest.py make --platform mac-arm64 --file Koan.dmg --url <url>
      --version 0.0.3-alpha.1 --chromium 158.0.8127.18 --notes-url <url>
      --key-file <ed25519 private key, PEM> --output koan-update.json
      Writes the manifest (a second `make` into the same file, with the same
      version and Chromium, adds or replaces just its platform). The new
      signature is verified with the public key derived from --key-file
      before anything is written.

  update_manifest.py verify --manifest koan-update.json --file Koan.dmg
      --platform mac-arm64 --public-key-file koan_update_public_key.txt
      Recomputes the file's size and SHA-256 and checks the signature.
      Exits non-zero, saying what differs, on any mismatch.

  update_manifest.py message --platform mac-arm64 --version <v>
      --sha256 <hex> --size <n>
      Prints the exact bytes that get signed (for the app's test vectors).

Manifest (schema 1; JSON with sorted keys, 2-space indent):

  {"schema": 1, "version": "0.0.3-alpha.1", "chromium": "158.0.8127.18",
   "notes_url": "...", "platforms": {"mac-arm64": {"url": "...",
   "size": 123, "sha256": "<hex>", "signature": "<base64, 64 bytes>"}}}

The signed message is the UTF-8 bytes of

  "koan-update-v1\\n" + platform + "\\n" + version + "\\n" + sha256 + "\\n"
  + size + "\\n"

with sha256 as 64 lowercase hex digits and size in decimal. The version is in
the message so that nobody can serve an older, genuinely signed disk image
labelled as a newer version: the signature would not match the label. The
platform is in it so an image of one platform can't be served for another.

Ed25519 is done by the `openssl` command (OpenSSL 3), so this stays standard
library only. The raw 32-byte public key is wrapped in the Ed25519
SubjectPublicKeyInfo prefix to give openssl a key it can read.
"""

import argparse
import base64
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src' / 'version'))
import koan_version  # noqa: E402

SCHEMA = 1
PLATFORMS = ('mac-arm64', 'linux-x86_64')
MESSAGE_PREFIX = 'koan-update-v1\n'
# DER of an Ed25519 SubjectPublicKeyInfo up to the 32 raw key bytes.
_SPKI_PREFIX = bytes.fromhex('302a300506032b6570032100')
_SHA256 = re.compile(r'[0-9a-f]{64}')
_CHUNK = 1 << 20


class ManifestError(Exception):
  pass


def check_platform(platform):
  if platform not in PLATFORMS:
    raise ManifestError('unknown platform %r (one of %s)' %
                        (platform, ', '.join(PLATFORMS)))
  return platform


def check_version(text):
  try:
    version = koan_version.parse(text)
  except ValueError as e:
    raise ManifestError(str(e))
  if version.text != text:
    raise ManifestError('%r is not a Koan version: no spaces around it' % text)
  return text


def check_chromium(text):
  try:
    koan_version.parse_chromium(text)
  except ValueError as e:
    raise ManifestError(str(e))
  if text != text.strip():
    raise ManifestError('%r is not a Chromium version' % text)
  return text


def check_sha256(text):
  if not _SHA256.fullmatch(text):
    raise ManifestError('%r is not a SHA-256 in lowercase hex' % text)
  return text


def check_size(size):
  if not isinstance(size, int) or isinstance(size, bool) or size < 0:
    raise ManifestError('%r is not a size' % (size,))
  return size


def check_url(url):
  if not url.startswith('https://') or any(c.isspace() for c in url):
    raise ManifestError('%r is not an https:// URL' % url)
  return url


def message(platform, version, sha256, size):
  """The bytes that are signed."""
  check_platform(platform)
  check_version(version)
  check_sha256(sha256)
  check_size(size)
  return ('%s%s\n%s\n%s\n%d\n' % (MESSAGE_PREFIX, platform, version, sha256,
                                  size)).encode('utf-8')


def file_digest(path):
  """(size, sha256 hex) of a file, read in chunks."""
  digest = hashlib.sha256()
  size = 0
  with open(path, 'rb') as f:
    while True:
      chunk = f.read(_CHUNK)
      if not chunk:
        break
      digest.update(chunk)
      size += len(chunk)
  return size, digest.hexdigest()


def _openssl(*args):
  try:
    return subprocess.run(['openssl', *args], capture_output=True)
  except FileNotFoundError:
    raise ManifestError('openssl is not installed')


def sign(key_file, data):
  """The 64-byte Ed25519 signature of `data` with a PEM private key."""
  with tempfile.TemporaryDirectory() as tmp:
    msg = os.path.join(tmp, 'message')
    out = os.path.join(tmp, 'signature')
    with open(msg, 'wb') as f:
      f.write(data)
    result = _openssl('pkeyutl', '-sign', '-rawin', '-inkey', str(key_file),
                      '-in', msg, '-out', out)
    if result.returncode:
      raise ManifestError('openssl could not sign with %s: %s' %
                          (key_file, result.stderr.decode(errors='replace')
                           .strip()))
    with open(out, 'rb') as f:
      signature = f.read()
  if len(signature) != 64:
    raise ManifestError('the key is not an Ed25519 key (got a %d-byte '
                        'signature)' % len(signature))
  return signature


def public_key_of(key_file):
  """The raw 32 bytes of the public key that goes with a PEM private key."""
  result = _openssl('pkey', '-in', str(key_file), '-pubout', '-outform', 'DER')
  if result.returncode:
    raise ManifestError('openssl could not read %s: %s' %
                        (key_file, result.stderr.decode(errors='replace')
                         .strip()))
  der = result.stdout
  if len(der) != len(_SPKI_PREFIX) + 32 or not der.startswith(_SPKI_PREFIX):
    raise ManifestError('%s is not an Ed25519 key' % key_file)
  return der[len(_SPKI_PREFIX):]


def read_public_key(path):
  """The raw 32 bytes from a file holding them in base64."""
  try:
    raw = base64.b64decode(pathlib.Path(path).read_text().strip(),
                           validate=True)
  except ValueError as e:
    raise ManifestError('%s is not base64: %s' % (path, e))
  if len(raw) != 32:
    raise ManifestError('%s holds %d bytes, not a 32-byte Ed25519 key' %
                        (path, len(raw)))
  return raw


def signature_is_valid(public_key, data, signature):
  """Whether `signature` is a valid Ed25519 signature of `data`."""
  if len(public_key) != 32 or len(signature) != 64:
    return False
  with tempfile.TemporaryDirectory() as tmp:
    paths = {}
    for name, content in (('key', _SPKI_PREFIX + public_key),
                          ('message', data), ('signature', signature)):
      paths[name] = os.path.join(tmp, name)
      with open(paths[name], 'wb') as f:
        f.write(content)
    result = _openssl('pkeyutl', '-verify', '-rawin', '-pubin', '-keyform',
                      'DER', '-inkey', paths['key'], '-in', paths['message'],
                      '-sigfile', paths['signature'])
  return result.returncode == 0


def entry(platform, file, url, version, key_file):
  """The manifest entry of one platform's disk image, signed."""
  size, sha256 = file_digest(file)
  data = message(platform, version, sha256, size)
  signature = sign(key_file, data)
  if not signature_is_valid(public_key_of(key_file), data, signature):
    raise ManifestError('the new signature does not verify with the public '
                        'key of %s' % key_file)
  return {
      'url': check_url(url),
      'size': size,
      'sha256': sha256,
      'signature': base64.b64encode(signature).decode('ascii'),
  }


def merge(existing, version, chromium, notes_url, platform, new_entry):
  """The manifest with `platform` added or replaced.

  Another platform's entry stays only if `existing` is the same release.
  """
  platforms = {}
  if (isinstance(existing, dict) and existing.get('schema') == SCHEMA and
      existing.get('version') == version and
      existing.get('chromium') == chromium and
      isinstance(existing.get('platforms'), dict)):
    platforms.update(existing['platforms'])
  platforms[platform] = new_entry
  return {
      'schema': SCHEMA,
      'version': version,
      'chromium': chromium,
      'notes_url': notes_url,
      'platforms': platforms,
  }


def dump(manifest):
  return json.dumps(manifest, sort_keys=True, indent=2) + '\n'


def verify(manifest_path, file, platform, public_key):
  """Raises ManifestError unless `file` is what the manifest signed."""
  check_platform(platform)
  try:
    manifest = json.loads(pathlib.Path(manifest_path).read_text())
  except ValueError as e:
    raise ManifestError('%s is not JSON: %s' % (manifest_path, e))
  if not isinstance(manifest, dict) or manifest.get('schema') != SCHEMA:
    raise ManifestError('%s is not a schema %d manifest' %
                        (manifest_path, SCHEMA))
  platforms = manifest.get('platforms')
  item = platforms.get(platform) if isinstance(platforms, dict) else None
  if not isinstance(item, dict):
    raise ManifestError('the manifest has no %s entry' % platform)
  version = manifest.get('version')
  if not isinstance(version, str):
    raise ManifestError('the manifest has no version')
  check_version(version)
  sha256 = item.get('sha256')
  size = item.get('size')
  if not isinstance(sha256, str):
    raise ManifestError('the %s entry has no sha256' % platform)
  check_sha256(sha256)
  check_size(size)
  try:
    signature = base64.b64decode(item.get('signature', ''), validate=True)
  except (ValueError, TypeError):
    raise ManifestError('the %s signature is not base64' % platform)
  if len(signature) != 64:
    raise ManifestError('the %s signature is %d bytes, not 64' %
                        (platform, len(signature)))
  actual_size, actual_sha256 = file_digest(file)
  if actual_size != size:
    raise ManifestError('size mismatch: %s is %d bytes, the manifest says %d' %
                        (file, actual_size, size))
  if actual_sha256 != sha256:
    raise ManifestError('SHA-256 mismatch: %s is %s, the manifest says %s' %
                        (file, actual_sha256, sha256))
  if not signature_is_valid(public_key,
                            message(platform, version, sha256, size),
                            signature):
    raise ManifestError('the signature does not verify for %s %s %s with '
                        'this public key' % (platform, version, sha256))


def _make(args):
  platform = check_platform(args.platform)
  version = check_version(args.version)
  chromium = check_chromium(args.chromium)
  check_url(args.notes_url)
  new_entry = entry(platform, args.file, args.url, version, args.key_file)
  existing = None
  output = pathlib.Path(args.output)
  if output.exists():
    try:
      existing = json.loads(output.read_text())
    except ValueError:
      existing = None
  manifest = merge(existing, version, chromium, args.notes_url, platform,
                   new_entry)
  output.write_text(dump(manifest))
  print('%s: %s %s, %d bytes, sha256 %s' %
        (output, platform, version, new_entry['size'], new_entry['sha256']))


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  sub = parser.add_subparsers(dest='command', required=True)
  p = sub.add_parser('make')
  p.add_argument('--platform', required=True)
  p.add_argument('--file', required=True)
  p.add_argument('--url', required=True)
  p.add_argument('--version', required=True)
  p.add_argument('--chromium', required=True)
  p.add_argument('--notes-url', required=True)
  p.add_argument('--key-file', required=True)
  p.add_argument('--output', required=True)
  p = sub.add_parser('verify')
  p.add_argument('--manifest', required=True)
  p.add_argument('--file', required=True)
  p.add_argument('--platform', required=True)
  p.add_argument('--public-key-file', required=True)
  p = sub.add_parser('message')
  p.add_argument('--platform', required=True)
  p.add_argument('--version', required=True)
  p.add_argument('--sha256', required=True)
  p.add_argument('--size', required=True, type=int)
  args = parser.parse_args(argv)

  try:
    if args.command == 'make':
      _make(args)
    elif args.command == 'verify':
      verify(args.manifest, args.file, args.platform,
             read_public_key(args.public_key_file))
      print('%s: %s matches the manifest and its signature' %
            (args.file, args.platform))
    else:
      sys.stdout.buffer.write(
          message(args.platform, args.version, args.sha256, args.size))
  except (ManifestError, OSError) as e:
    print('update_manifest.py: %s' % e, file=sys.stderr)
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
