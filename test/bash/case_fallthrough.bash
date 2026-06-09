#!/bin/bash
for x in a b c d; do
  case $x in
    a) echo "got a" ;;
    b) echo "got b" ;& 
    c) echo "fellthrough or c" ;;
    d) echo "got d" ;;
  esac
done
echo "--- continue match ---"
case hello in
  h*) echo "starts h" ;;&
  *o) echo "ends o" ;;&
  hello) echo "exact" ;;
  *) echo "default" ;;
esac
echo "--- fall to last ---"
case 1 in
  1) echo one ;&
  2) echo two ;;
esac
