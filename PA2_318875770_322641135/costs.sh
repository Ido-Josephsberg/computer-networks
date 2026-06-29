#!/bin/sh
awk -F',' -v t="$2" '$1 == t || $2 == t { print $3 }' $1
