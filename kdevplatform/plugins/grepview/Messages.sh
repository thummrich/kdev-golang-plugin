#!/bin/sh
$EXTRACTRC `find . -name \*.rc` `find . -name \*.ui` >>rc.cpp
$XGETTEXT `find . -name \*.cc -o -name \*.cpp -o -name \*.h` -o $podir/kdevgrepview.pot
rm -f rc.cpp
