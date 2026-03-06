#!/bin/bash

SRC="/sdcard/download"

if (( $# % 3 != 0  || $# == 0 )); then
  echo "Format: name dest_hxx dest_cxx"
  exit 1
fi

while [ $# -gt 0 ]; do
name="$1"
dest_hxx="$2"
dest_cxx="$3"

mv "$SRC/$name.hxx" "$dest_hxx/"
mv "$SRC/$name.cxx" "$dest_cxx/"

shift 3
done
