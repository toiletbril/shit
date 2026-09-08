#!/usr/bin/env bash
# A case arm inside a command substitution. The parenthesis that ends a pattern
# closes no region, so the substitution reaches its own closing parenthesis.

echo "plain=$(case x in x) echo unparenthesized ;; esac)"
echo "paren=$(case x in (x) echo parenthesized ;; esac)"
echo "tick=`case x in x) echo backtick ;; esac`"
echo "alt=$(case b in a|b) echo alternation ;; esac)"
echo "nest=$(case x in x) case y in y) echo nested ;; esac ;; esac)"
echo "sub=$(case x in x) echo body-$( echo inner ) ;; esac)"
echo "word=$(case esac in esac) echo pattern-esac ;; esac)"

# A keyword that introduces a command keeps the next word at command position,
# so a case header after do, then, or a brace is still recognized.
echo "loop=$(for item in one two; do case $item in one) echo first ;; *) echo rest ;; esac; done)"
echo "cond=$(if true; then case x in x) echo conditional ;; esac; fi)"
echo "brace=$( { case x in x) echo braced ;; esac; } )"
echo "bang=$(! false && case x in x) echo negated ;; esac)"

# A word that only looks like a keyword is not one.
echo "words=$(echo case; echo in; echo esac)"

# The fallthrough terminators of bash end an arm without closing the region.
echo "fall=$(case x in x) echo semi ;& *) echo next ;; esac)"
echo "retry=$(case x in x) echo first ;;& x) echo again ;; esac)"

# A case arm inside a process substitution reads through the same scanner.
cat < <(case x in x) echo procsub ;; esac)
while read -r line; do
  echo "read:$line"
done < <(case y in y) printf 'a\nb\n' ;; esac)

# A function body, an arithmetic expansion, and a quoted parenthesis inside the
# arm all keep the region balanced.
pick() {
  case $1 in
  small) echo "under ten" ;;
  *) echo "ten or more" ;;
  esac
}
echo "fn=$(pick small) $(pick large)"
echo "math=$(case $((2 + 2)) in 4) echo four ;; esac)"
echo "quote=$(case x in x) echo "literal )" ;; esac)"

# A heredoc body inside the arm is skipped before the region closes.
echo "heredoc=$(case x in x) cat <<EOF
inside )
EOF
;; esac)"
