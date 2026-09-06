unset KOSH_FLAGS
# The tab selector picks how several completion candidates are presented. The
# session value comes from --tab-selector, then from the plain listing --dumb
# asks for, and the set builtin reports and changes it at runtime.
echo "== the default session selector:"
"$BIN" -c 'set --tab-selector'
echo "== --tab-selector plain:"
"$BIN" --tab-selector plain -c 'set --tab-selector'
echo "== --tab-selector external:"
"$BIN" --tab-selector=external -c 'set --tab-selector'
echo "== --dumb selects plain:"
"$BIN" --dumb -c 'set --tab-selector'
echo "== an explicit value wins over --dumb:"
"$BIN" --dumb --tab-selector interactive -c 'set --tab-selector'
echo "== an unknown command line value is rejected:"
"$BIN" --tab-selector bogus -c 'echo unreachable'
echo "status=$?"
echo "== the set builtin changes it at runtime:"
"$BIN" -c 'set --tab-selector external; set --tab-selector'
echo "== a mood change keeps the selector:"
"$BIN" -c 'set --tab-selector plain; set --mood bash; set --tab-selector'
echo "== an unknown set value is rejected:"
"$BIN" --no-annoying-diagnostics -c 'set --tab-selector bogus'
echo "status=$?"
echo "== the selector survives a subshell:"
"$BIN" -c 'set --tab-selector external; (set --tab-selector)'
