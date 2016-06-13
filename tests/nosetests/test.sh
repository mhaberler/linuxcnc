#!/bin/bash

FLAGS="-v"
nt=`which nosetests`
if [ "$nt" == "" ]; then
    echo nosetests not installed
    exit 1
fi
for t in $EMC2_HOME/nosetests/*.py
do
    nosetests  $FLAGS  $t || exit 1
done
