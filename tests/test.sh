#!/bin/bash

CKSUM0=`md5sum "${srcdir}/tests/gplv3.txt.gzi.ORIG" | awk '{print $1;}'`
echo gplv3.txt.gzi.ORIG CHECKSUM = $CKSUM0

`"$gztool_abspath" -fxi "${srcdir}/tests/gplv3.txt.gz"`

CKSUM1=`md5sum "${srcdir}/tests/gplv3.txt.gzi" | awk '{print $1;}'`
echo gplv3.txt.gzi CHECKSUM = $CKSUM1

if [ "$CKSUM0" = "$CKSUM1" ]; then
	rm "${srcdir}/tests/gplv3.txt.gzi"
	exit 0;
else
	echo $CKSUM0 != $CKSUM1;
	exit 1;
fi
