#!/usr/bin/env bash

echo perl check

script=$1
[[ "$script" ]] || exit 1
[[ -f "$script" ]] || exit 1

perl -c $script
res=$?

if [[ $res -ne 0 ]]; then
  echo syntax error
  exit $res
fi
