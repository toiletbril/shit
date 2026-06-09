#!/bin/bash
type -t echo
type -t if
type -t while
f() { :; }
type -t f
type -t totally_nonexistent_xyz_cmd
echo "notfound rc=$?"
