#!/bin/sh

DEBUG=5 realtime restart ;

for i in $EMC2_HOME/src/hal/i_components/*.icomp ; do
    basename -s .icomp "$i" >> result ;
    compname=$(basename -s .icomp "$i") ;
    halcmd loadrt $compname ;
    halcmd list pin >> result ;
    halcmd unloadrt $compname ;
done

halrun -U

