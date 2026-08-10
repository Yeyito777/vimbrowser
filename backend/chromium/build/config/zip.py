#!/usr/bin/env python3
# Copyright 2014 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create or merge zip archives for platform-independent build actions."""

import argparse
import fnmatch
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

import action_helpers
import zip_helpers


def main(argv):
  parser = argparse.ArgumentParser()
  parser.add_argument('--input-files', help='GN-list of files to zip.')
  parser.add_argument(
      '--input-files-base-dir',
      help='Paths in the archive will be relative to this directory.')
  parser.add_argument('--input-zips', help='GN-list of archives to merge.')
  parser.add_argument('--input-zips-excluded-globs',
                      help='GN-list of archive paths to exclude.')
  parser.add_argument('--output', required=True, help='Output archive path.')
  compression = parser.add_mutually_exclusive_group()
  compression.add_argument('--compress', action='store_true')
  compression.add_argument('--no-compress', action='store_false', dest='compress')
  parser.add_argument('--comment-json',
                      action='append',
                      metavar='KEY=VALUE',
                      type=lambda value: value.split('=', 1))
  action_helpers.add_depfile_arg(parser)
  options = parser.parse_args(argv)

  with action_helpers.atomic_output(options.output) as output:
    with zipfile.ZipFile(output.name, 'w') as output_zip:
      depfile_inputs = None
      if options.input_files:
        zip_helpers.add_files_to_zip(
            action_helpers.parse_gn_list(options.input_files),
            output_zip,
            base_dir=options.input_files_base_dir,
            compress=options.compress)

      if options.input_zips:
        depfile_inputs = action_helpers.parse_gn_list(options.input_zips)
        excluded = action_helpers.parse_gn_list(
            options.input_zips_excluded_globs) if options.input_zips_excluded_globs else []
        path_transform = None
        if excluded:
          path_transform = lambda path: None if any(
              fnmatch.fnmatch(path, pattern) for pattern in excluded) else path
        zip_helpers.merge_zips(output_zip,
                               depfile_inputs,
                               path_transform=path_transform,
                               compress=options.compress)

      if options.comment_json:
        output_zip.comment = json.dumps(dict(options.comment_json),
                                        sort_keys=True).encode('utf-8')

  if options.depfile:
    action_helpers.write_depfile(options.depfile,
                                 options.output,
                                 inputs=depfile_inputs)


if __name__ == '__main__':
  main(sys.argv[1:])
