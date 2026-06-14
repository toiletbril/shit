#!/bin/bash
# A local -r marks the name read-only only for its own scope, so calling the
# function a second time redeclares and reassigns it rather than failing as
# read-only, the way sdkman's completion declares version_paths.
f() {
	local -r vp=("a" "b" "c")
	echo "vp=${vp[1]} count=${#vp[@]}"
}
f
f
g() {
	local -r name="val"
	echo "name=$name"
}
g
g
