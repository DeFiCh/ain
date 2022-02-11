#!/bin/sh
# Copyright (c) 2012-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C
if [ $# -gt 1 ]; then
    cd "$2" || exit 1
fi
if [ $# -gt 0 ]; then
    FILE="$1"
    shift
    if [ -f "$FILE" ]; then
        INFO="$(head -n 1 "$FILE")"
    fi
else
    echo "Usage: $0 <filename> <srcroot>"
    exit 1
fi

git_check_in_repo() {
    ! { git status --porcelain -uall --ignored "$@" 2>/dev/null || echo '??'; } | grep -q '?'
}

DESC=""
SUFFIX=""
CURRENT_BRANCH=""
if [ "${BITCOIN_GENBUILD_NO_GIT}" != "1" ] && [ -e "$(command -v git)" ] && [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" = "true" ] && git_check_in_repo share/genbuild.sh; then
    # clean 'dirty' status of touched files that haven't been modified
    git diff >/dev/null 2>/dev/null

    # if latest commit is tagged and not dirty, then override using the tag name
    RAWDESC=$(git describe --tags --abbrev=0 2>/dev/null)
    if [ "$(git rev-parse HEAD)" = "$(git rev-list -1 $RAWDESC 2>/dev/null)" ]; then
        git diff-index --quiet HEAD -- && DESC=$RAWDESC
    fi

    SUFFIX=$(git rev-parse --short HEAD)
    CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
    # Move to this after git upgrade from base images 
    # CURRENT_BRANCH="$(git branch --show-current)"
    
    if [ -n "$CURRENT_BRANCH" ]; then
        # Make sure to replace `/` with `-`. Since this is
        # executed with posix shell, cannot do bashisms.
        SUFFIX="$(echo $CURRENT_BRANCH | sed 's/\//-/g')-$SUFFIX"
    fi

    if [ "$CURRENT_BRANCH" != "hotfix" ] && [ "$CURRENT_BRANCH" != "master" ]; then
        # if it's hotfix branch, don't mark dirty.
        # otherwise generate suffix from git, i.e. string like "59887e8-dirty". 
        git diff-index --quiet HEAD -- || SUFFIX="$SUFFIX-dirty"
    fi
fi

echo "BUILD_GIT_BRANCH: $CURRENT_BRANCH"
echo "BUILD_SUFFIX: $SUFFIX"

if [ -n "$DESC" ]; then
    NEWINFO="#define BUILD_DESC \"$DESC\""
elif [ -n "$SUFFIX" ]; then
    NEWINFO="#define BUILD_SUFFIX $SUFFIX"
else
    NEWINFO="// No build information available"
fi

# only update build.h if necessary
if [ "$INFO" != "$NEWINFO" ]; then
    echo "$NEWINFO" >"$FILE"
fi
