#!/bin/sh
set -e -x

[ "$TEST" = "YES" ] || exit 0

make tapfiles

find . -name '*.tap' -print0 | xargs -0 -n1 prove -e cat -f
