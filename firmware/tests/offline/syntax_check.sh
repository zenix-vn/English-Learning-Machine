#!/bin/sh
# Type-checks the ESP glue (storage, sync, study) against the stub declarations. Needs g++ 13+.
set -eu
cd "$(dirname "$0")"
INC="-Istubs/include -I../../main/offline -I../../main"
for fonts in 1 0; do          # with and without the optional generated fonts
  for f in storage sync study_view study; do
    echo "checking $f.cc (fonts=$fonts)"
    g++ -std=c++23 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -DCONFIG_EL_OFFLINE_NEW_PER_DAY=10 \
        -DEL_HAVE_FONT_VI=$fonts -DEL_HAVE_FONT_VI22=$fonts -DEL_HAVE_FONT_EN44=$fonts $INC ../../main/offline/$f.cc
  done
done
echo "glue type-check OK"
