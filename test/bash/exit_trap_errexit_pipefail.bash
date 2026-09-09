#!/bin/bash
# The EXIT trap against a pipeline that pipefail and errexit end together,
# checked against bash.
trap 'echo "action_saw=$?"' EXIT

set -e
set -o pipefail
echo before
false | /bin/cat
echo unreachable
