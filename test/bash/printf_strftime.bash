#!/bin/bash
# The printf %(fmt)T time conversion, checked against bash. A fixed epoch
# renders a fixed time so the result is stable, and both shells read the same
# zone from the environment.
printf '%(%Y-%m-%d)T\n' 1700000000
printf '[%(%H:%M:%S)T]\n' 0
printf '%(%Y)T\n' 1000000000
printf 'year=%(%Y)T month=%(%m)T\n' 1700000000 1700000000
