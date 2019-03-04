#!/usr/bin/env bash

echo python check

script=$1
[[ "$script" ]] || exit 1
[[ -f "$script" ]] || exit 1

python -m py_compile $script
res=$?
[[ -f "$script"c ]] && rm "$script"c 2>/dev/null

if [[ $res -ne 0 ]]; then
  echo syntax error
  exit $res
else
  echo syntax ok
fi

if [[ "`which pyflakes`" ]]; then
  pyflakes $script
  res=$?

  if [[ $res -ne 0 ]]; then
    echo "content (logical) error"
    exit $res
  else
    echo content ok
  fi
fi

if [[ "`which pylint`" ]]; then
  pylint -E $script
  res=$?

  if [[ $res -ne 0 ]]; then
    echo "pylint (style) warning"
  else
    echo pylint passed
  fi
fi

echo ok
