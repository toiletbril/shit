#!/bin/bash
# A not-found command in a pipeline stage does not abort the rest, so a later
# stage still runs and the pipeline status is the last stage's, matching bash.
nonexistent_cmd_xyz_123 | cat
echo "after=$?"
echo start | nonexistent_cmd_xyz_123 | wc -l | tr -d ' '
