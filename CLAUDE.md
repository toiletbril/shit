# shit project notes

This file records the conventions and the architecture of the shell so a
session does not relearn them from scratch. shit is a C++ and C shell where
speed is the defining goal. The interactive editor is a vendored C submodule
under src/toiletline.

## Build

The Makefile is src/Makefile. `make MODE=rel` writes the optimized binary to
../shit, `make MODE=dbg` writes ../shit-dbg with AddressSanitizer and the
UndefinedBehaviorSanitizer, and `make MODE=cov` writes ../shit-cov. The default
mode is dbg. A bare `make` builds the shell into its object tree from a clean
checkout, since the object directories are created as an order-only prerequisite
and the default goal is the shit target. The completion suite needs the debug
binary, since `--debug-complete-at` is gated behind NDEBUG. Prefer a make target
over a raw compiler call, and clean a stale artifact with `make clean`, never a
bare `rm` of ../shit. The full MODE catalog, the cross-compilation targets, and
the install with its PREFIX are documented in the README.

## Moods

The flags, the moods, and the UX behavior are documented in the man page
docs/shit.1, read with `mandoc docs/shit.1`. The man page is the source of truth,
and a flag, a mood, or a UX change updates it. The architecture notes below cover
what the man page does not.

The mood drives the nounset, pipefail, and failglob strictness through
`apply_strictness_for_mood`. An explicit `set -u`, `set -o pipefail`, or `set -o
failglob` survives a later mood switch through the per-toggle explicit marks. A
glob in command position is fatal in the default mood and a warning in a
compatibility mood, checked in SimpleCommand::evaluate_impl through
command_word_is_glob. The mood, the diagnostics toggles, and the three strictness
toggles with their explicit marks live in one `runtime_state` struct (Eval.hpp),
which capture and restore copy whole. Main.cpp enters rescue rather than exiting
when a flag fails to parse in a login shell, the lockout-risk case marked by a
dash-prefixed argv[0], while any other invocation keeps the usage exit.

All errors are located in all moods.

## Code conventions

Locals use `let` and `let const`, the macros for `auto` and `const auto`, so a
deducible type is never spelled out. A literal counter such as `usize i = 0`
keeps its type, because `let i = 0` would deduce int rather than the unsigned
type. Functions use the `fn name(...) throws -> ret` macro form. A null pointer
check reads `!= nullptr` or `== nullptr`, never a bare truthiness test.

Names are verbose and semantic, never terse. A boolean reads `is_`, `should_`,
`was_`, `did_`, or `has_`. A number carries a `_count` suffix or a measure suffix
such as `_length`, `_depth`, or `_position`, and never a bare `n_` prefix. A
variable-bound lambda is named `do_`. An accessor reads `get_` or `set_`. A clear
name replaces a comment that would explain an unclear one. A comment states why
the code is the way it is, not what it does.

An if whose condition has `&&` or `||` is braced, while a trivial
single-condition if stays unbraced. Logical blocks are separated by a blank line,
before and after a loop, before a return, and after a group of declarations.

A chain of three or more name comparisons becomes a static table rather than an
if ladder. A hot dispatch on a leading byte becomes a switch. A static dispatch
table is a `consteval StaticStringMap` whose keys are written through the `SSK`
macro.

Stray enums and structs are lower_snake_case. Only a class, a class nested enum,
and a nested type are CamelCase. File operations take a Path, not a String or a
StringView.

State threads through EvalContext and through constructors. The codebase holds no
mutable global for per-executor state, beyond a small audited set in
docs/globals-audit.md. New abstractions, file splits or merges, and dependency
upgrades wait for approval. Before implementing anything new, search for an
existing function, parser, or helper and reuse it rather than writing a second
copy.

## Architecture

src/Main.cpp is the entry point, reading the flags, running the rc chain, and
driving the interactive loop or the script. src/Lexer.cpp turns source into
tokens, src/Parser.cpp builds the syntax tree, and src/Optimizer.cpp folds
constants and prunes dead branches in the analysis prepass. A C-style for whose
condition folds to zero drops the whole loop only when its init clause is blank,
since a non-blank init runs once before the condition the way C semantics
require.

Evaluation spreads across src/Eval.cpp and the Eval prefixed files, which split
substitution, word expansion, parameter expansion, globbing, arithmetic, arrays,
source, jobs, and functions into their own units. src/Expressions.cpp holds the
command node base and the analysis hooks, with the simple command in
src/ExpressionsSimpleCommand.cpp, the lists, pipeline, loops, case, and compound
commands in src/ExpressionsCompound.cpp, and the arithmetic and logical nodes in
src/ExpressionsArith.cpp. Shared free helpers declare in
src/ExpressionsInternal.hpp. The builtins live under src/builtins, and the
busybox-style coreutils live under src/shitbox.

src/Platform.cpp wraps the operating system behind an os namespace, with the
POSIX block and the Windows block defining the same API twice on purpose.

src/Completion.cpp drives zero config completion. Completion first slices the
buffer to the command segment holding the cursor, and the slice is quote aware,
so a `;`, `|`, `&`, or newline inside quotes does not start a new segment. The
cascade tries the process arguments of kill and its kin, the builtin flags, the
registered specs, the build tool targets, the man subcommands, the manpage
options, the help subcommands, the help options, and finally the filesystem. A
candidate is matched by smart case and then by subsequence, so an all lowercase
token matches either case and `fbb` matches `foo_bar_baz`, while an exact prefix
always ranks first. A command-position path offers only runnable files and the
directories, and a known utility floats the files whose extension it operates on
ahead of the rest. A command forks its `--help` at most once per cache key,
behind an allowlist and a trusted directory gate, and the subcommand walk stops
at a dash-led word, an unknown subcommand, or MAX_SUBCOMMAND_DEPTH of four. The cascade splits across
src/Completion.cpp, src/CompletionManpage.cpp, src/CompletionScan.cpp, and the
per-keystroke highlighter in src/CompletionHighlight.cpp, with shared helpers in
src/CompletionInternal.hpp. The per-program policy tables, the --help allowlist,
the extension hints, the custom-completer routing, and the transparent prefixes,
live in src/CompletionPolicy.hpp, so a program absent from every table falls
through to the manpage and the filesystem. The highlighter and TAB completion share one
most-recently-used cache of directory listings keyed by path and invalidated by
mtime. src/Toiletline.cpp bridges the editor to the evaluator, and
src/toiletline/toiletline.h is the editor. The `--debug-highlight-at` flag, a
debug-only test driver gated behind NDEBUG like `--debug-complete-at`, prints the
highlight spans for a line so the highlighter is testable without the editor.

src/Errors.cpp renders the located caret and the trailing note, capitalized on
its own line, with the shellcheck-style messages in src/Diagnostics.hpp. A note
is baked in at construction through the four detail-carrying classes,
ErrorWithDetails, WarningWithDetails, ErrorWithLocationAndDetails, and
WarningWithLocationAndDetails, so the ordinary Error and ErrorWithLocation hold
none and there is no set_note setter. The relocate_error bridge rewraps an
unlocated error onto a span, rethrows it, and preserves the note by choosing the
located-and-details form. The
shell normalizes SIGPIPE, SIG_IGN for the main shell and SIG_DFL in a forked
child so a producer dies with status 141. The cd builtin resolves a relative
operand against the logical PWD, the bash -L mode, and cd .. lexically pops the
last component. PIPESTATUS is published after every foreground command.

## Header factoring and value-type methods

Small types live in their own lightweight headers, the mimic_mood enum in
src/MimicMood.hpp, the RuntimeState class in src/RuntimeState.hpp, and the
name-value split in src/NameValueArg.hpp whose NameValueArg::from factory
performs the split. A factored data structure lives directly in the shit
namespace, and a factored class method is defined inline in its header. A free
helper whose receiver is a value type is a method on that type, such as
StringView::is_all_decimal_digits, String::replace, and Path::read_entire_file.
find_pos_in_vec returns Maybe<usize> rather than a sentinel. Logic shared by the
POSIX and Windows blocks of Platform.cpp lives in Utils.cpp.

## Memory discipline

The custom allocator and the containers live in Arena.cpp, String.cpp, and the
headers. ArrayList does not allocate on default construction and grows
geometrically, String carries a small inline buffer and grows geometrically, and
a scratch arena follows a mark and release lifetime per scope.

## Testing

Run `make -C test test` for the main suite and the completion suite under the
debug binary, and `make -C test bench` for the benchmark. Wrap an interactive
launch in a timeout. The `refill` target regenerates goldens, and the `REFILL`
variable scopes it to named tests. Read each regenerated golden before trusting
it, since refill blesses whatever the binary prints. The shit_tests, cli_tests,
dashdiff, bashdiff, mimicrydiff, and bench recipes live in per-target scripts
under test/, launched with /bin/bash. The shit_tests and cli_tests scripts take
the names to run as arguments, so a bare NAME or a cli_NAME target runs one test
through the same script. The dashdiff, bashdiff, and mimicrydiff scripts compare
through process substitution. macOS diverges on a few tests, and the harness
carries alternate goldens.

The bashdiff and mimicrydiff bash comparisons require a bash 5.3 or newer
reference, since the goldens encode bash 5.x behavior, and both scripts skip the
bash comparison loudly when `$BASH` is older. The macOS system `/bin/bash` is
bash 3.2, so a macOS run passes the modern bash through `BASH`, as in `make -C
test bashdiff BASH=/opt/homebrew/bin/bash`.

## Finishing a change

Before finishing a plan, update this CLAUDE.md, the README, docs/shit.1, and
completions/shit.bash so the docs, the manual, and the completions stay in sync.
A new flag, a new mood, a new builtin, or a renamed option touches all four. The
flag, mood, and UX documentation lives in the man page docs/shit.1, so that file
is the primary write for such a change and this CLAUDE.md only routes to it. A
flag also updates completions/shit.bash so the completion offers it.

## Logging and debugging

The log macros live in src/Trace.hpp. LOG(level, fmt, ...) prints at or below the
active verbosity and LOG_VARS dumps named variables, across the levels Nothing,
Info, Debug, and All. TRACE compiles out of a release build while LOG stays. The
`-X` flag, long `--debug-logging`, turns logging on at a level, and
`--debug-logging-file` appends it to a file. The `--show-optimizer-state` flag
traces the prepass decisions and prints a located line for every eliminated node.
