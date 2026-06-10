#!/bin/bash
# A keyword such as esac serves as the matched word after case, the lines the
# bash suite runs in case.tests.
case esac in (esac) echo esac-matched;; esac
case for in for) echo for-matched;; *) echo no;; esac
x=if
case $x in if) echo if-matched;; esac
