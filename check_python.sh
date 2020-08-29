#!/usr/bin/env bash

echo python3 check

script=$1
[[ "$script" ]] || exit 1
[[ -f "$script" ]] || exit 1

python3 -m py_compile $script
res=$?

if [[ $res -ne 0 ]]; then
  echo syntax error
  exit $res
else
  echo syntax ok
fi

if [[ "`which pyflakes3`" ]]; then
  echo "pyflakes3 $script"
  pyflakes3 $script
  res=$?

  if [[ $res -ne 0 ]]; then
    echo content error
    exit $res
  else
    echo content ok
  fi
fi

if [[ "`which pylint3`" ]]; then
  echo "pylint3 -E $script"
  pylint3 -E $script
  res=$?

  if [[ $res -ne 0 ]]; then
    echo pylint3 warning
  else
    echo pylint3 passed
  fi
fi

# optional cleanup
#[[ -f "$script"c ]] && rm -f "$script"c

#
# local module include check
#
#	here=`pwd`
#	TEMPDIR=`mktemp -d /tmp/check.XXXXXXXXXX`
#	cd $TEMPDIR
#	# only system modules
#	MODULENAMES=$(python3 -c 'help("modules")' 2>/dev/null | sed -re '/^Please wait|^Enter any|^for modules/d')
#	cd $where
#	rmdir $TEMPDIR
#
#	declare -A modulenames
#	for n in $MODULENAMES ; do
#		modulenames[$n]=1
#	done
#
#	IMPORTS=$(egrep '(^import |^from )' $script | tr -d ',' | sed -re 's/\<(import|from|as .*)\>//g')
#	for w in $IMPORTS; do
#		if [[ "${modulenames[$w]}" ]] && [[ -f "$w.py" ]] ; then
#			echo "WARNING: import from local file ($w.py) conflicts global module ($w)"
#		fi
#	done

echo ok
