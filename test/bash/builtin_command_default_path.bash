#!/bin/bash
# command -p resolves against a default PATH that finds the standard utilities,
# so it runs echo even when the caller's PATH is empty.
echo "normal=$(command -p echo hi)"
echo "empty_path=$(PATH= command -p echo hi)"
