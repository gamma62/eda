#!/usr/bin/env bash

which xmllint >/dev/null || exit 1

XML=${1:-build.xml}
if ! [ -f ${XML} ]; then
  echo "ERROR: no input file"
  exit 1
fi

TEMPDIR=`mktemp -d /tmp/inst.XXXXXXXXXX`
touch $TEMPDIR/.err

xmllint  --valid  ${XML} > $TEMPDIR/control.xml 2> $TEMPDIR/.err
if [ ! -s $TEMPDIR/control.xml ]; then
  echo "ERROR: validation error, xmllint failed"
  cat $TEMPDIR/.err
  exit 1
fi
echo xmllint run...

if [ `cat $TEMPDIR/.err | wc -l` -eq 3 ] &&\
egrep -v '^[^:]+:2: validity error : Validation failed: no DTD found' $TEMPDIR/.err &>/dev/null
then
  rm $TEMPDIR/.err
else
  echo "ERROR: validation error"
  cat $TEMPDIR/.err
  exit 1
fi
echo no error...

diff -B -w -q ${XML} $TEMPDIR/control.xml >/dev/null
err=$?
if [ ${err} -ne 0 ]; then
  echo "WARNING: difference found, maybe an error, see $TEMPDIR/control.xml"
  diff -B -w -u ${XML} $TEMPDIR/control.xml
  echo "diffuse ${XML} $TEMPDIR/control.xml"
else
  rm -rf $TEMPDIR
  echo ok
fi

exit $err
