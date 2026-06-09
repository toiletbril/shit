#!/bin/bash
local x 2>/dev/null && echo "set" || echo "notset rc=$?"
cd /nonexistent_dir_xyz 2>/dev/null
echo "after cd rc=$?"
unset -- 2>/dev/null
echo "continued"
