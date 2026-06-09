#!/bin/sh
# umask reads and sets the file-creation mask in octal and symbolic forms,
# checked against dash. Each step sets the mask before reading it, so the result
# does not depend on the inherited mask.

umask 022
umask
umask -S

umask 027
umask -S

umask u=rwx,g=rx,o=
umask

umask 022
umask g-w
umask

umask 000
umask -S

umask 022
umask a+x
umask -S
