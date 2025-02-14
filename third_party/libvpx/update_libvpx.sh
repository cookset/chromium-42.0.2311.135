#!/bin/bash -e
#
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This tool is used to update libvpx source code with the latest git
# repository.
#
# Make sure you run this in a git checkout of deps/third_party/libvpx!

# Usage:
#
# $ ./update_libvpx.sh [branch | revision | file or url containing a revision]
# When specifying a branch it may be necessary to prefix with origin/

# Tools required for running this tool:
#
# 1. Linux / Mac
# 2. git

export LC_ALL=C

# Location for the remote git repository.
GIT_REPO="http://git.chromium.org/webm/libvpx.git"

GIT_BRANCH="origin/master"
LIBVPX_SRC_DIR="source/libvpx"
BASE_DIR=`pwd`

if [ -n "$1" ]; then
  GIT_BRANCH="$1"
  if [ -f "$1"  ]; then
    GIT_BRANCH=$(<"$1")
  elif [[ $1 = http* ]]; then
    GIT_BRANCH=`curl $1`
  fi
fi

prev_hash="$(egrep "^Commit: [[:alnum:]]" README.chromium | awk '{ print $2 }')"
echo "prev_hash:$prev_hash"

rm -rf $LIBVPX_SRC_DIR
mkdir $LIBVPX_SRC_DIR
cd $LIBVPX_SRC_DIR

# Start a local git repo.
git clone $GIT_REPO .

# Switch the content to the latest git repo.
git checkout -b tot $GIT_BRANCH

add="$(git diff-index --diff-filter=A $prev_hash | \
tr -s [:blank:] ' ' | cut -f6 -d\ )"
delete="$(git diff-index --diff-filter=D $prev_hash | \
tr -s [:blank:] ' ' | cut -f6 -d\ )"

# Output the current commit hash.
hash=$(git log -1 --format="%H")
echo "Current HEAD: $hash"

# Output log for upstream from current hash.
if [ -n "$prev_hash" ]; then
  echo "git log from upstream:"
  pretty_git_log="$(git log \
                    --no-merges \
                    --topo-order \
                    --pretty="%h %s" \
                    $prev_hash..$hash)"
  if [ -z "$pretty_git_log" ]; then
    echo "No log found. Checking for reverts."
    pretty_git_log="$(git log \
                      --no-merges \
                      --topo-order \
                      --pretty="%h %s" \
                      $hash..$prev_hash)"
  fi
  echo "$pretty_git_log"
fi

# Git is useless now, remove the local git repo.
rm -rf .git

# Add and remove files.
echo "$add" | xargs -I {} git add {}
echo "$delete" | xargs -I {} git rm {}

# Find empty directories and remove them.
find . -type d -empty -exec git rm {} \;

chmod 755 build/make/*.sh build/make/*.pl configure

cd $BASE_DIR
