# Globals audit

The shell holds no mutable global for per-executor state. The state that drives
one command threads through EvalContext and through constructors, so a subshell
or a nested evaluation carries its own copy. This file lists the small audited
set of file-scope mutable globals and function-local statics that remain, and
it records why a global is acceptable for each one. Every entry is
single-threaded, since the evaluator runs on one thread and a worker thread uses
libc malloc directly rather than the shared pool.

A new global is not added without an entry here.

## Parse arenas

The active parse owns these pointers. Main.cpp aims each at a stack arena per
invocation and clears it after, so the ownership is a single active parse.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `AST_ARENA` | src/Arena.cpp | The arena the active parse allocates its syntax tree into. A cached AST is guarded by the `reset_generation` key. |
| `FUNCTION_ARENA` | src/Arena.cpp | The arena that function-body token segments allocate into, so a definition outlives the per-command AST reset. |
| `PROMPT_COMMAND_ARENA` | src/Main.cpp | The arena that holds the parsed PROMPT_COMMAND tree across prompts. The per-command reset never touches it, so the cached tree pointer stays valid. |

## Allocator

| Name | File | Purpose |
| ---- | ---- | ------- |
| heap pool (function-local static in `heap_allocator`) | src/Allocator.hpp | The process-wide size-classed free list backing the heap allocator. One process owns one allocator cache. The pool is trivially destructible, so there is no static-destruction-order hazard at exit. |

## Signal-handler state

A signal handler writes only these flags, since a flag is the one
async-signal-safe channel out of a handler. Each is drained on the main thread.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `INTERRUPT_REQUESTED` | src/Platform.cpp | The Ctrl-C flag set by the SIGINT handler and polled by the evaluator and the blocking read and poll loops. It is reset at each prompt and after a fork. |
| `CHILD_STATE_CHANGED` | src/Platform.cpp | Set by the SIGCHLD handler so the job layer knows a child changed state and must be reaped. |
| `SIGNAL_PENDING` | src/Platform.cpp | Set by the trapped-signal and SIGCHLD handlers to flag a pending trap dispatch. |
| `PENDING_SIGNAL_FLAGS` | src/Platform.cpp | A per-signal-number bit array set by the trapped-signal handler and drained by the trap dispatcher. |

## Registrar tables

Each table is filled once at static init by a registrar macro and is read-only
after main starts.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `BUILTIN_FLAG_LISTS` | src/Builtin.cpp | A builtin kind mapped to its static flag list. |
| `SHITBOX_UTIL_FLAG_LISTS` | src/Shitbox.cpp | A shitbox utility kind mapped to its static flag list. |

## PATH and command caches

The command hash mirrors dash. A PATH change is detected by comparing the last
observed value, and a cd or a hash -r marks the cache stale. The stale flag
defers the clear to the next lookup, so a run that resolves no further command
pays nothing.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `MAYBE_PATH` | src/Utils.cpp | The last observed PATH value, the change detector for the caches below and the source the resolver reads. |
| `PATH_CACHE` | src/Utils.cpp | An extension-stripped command name mapped to every absolute path it resolves to. |
| `PATH_CACHE_IS_STALE` | src/Utils.cpp | Marks the command cache stale so the next lookup drops it and re-resolves. |
| `PATH_MAP_IS_EAGER` | src/Utils.cpp | Records that interactive setup seeded the whole map, so a PATH change rebuilds eagerly rather than lazily. |
| `DIR_LISTING_CACHE` | src/Utils.cpp | Each PATH directory's filenames keyed by directory text, so a shared directory is read from disk once. |
| `CACHED_PATH_DIRS` | src/Utils.cpp | The deduplicated split of PATH, reused so a cold miss does not re-split. |
| `CACHED_PATH_DIRS_VALID` | src/Utils.cpp | The validity flag for the split cache. |
| `CACHED_SPLIT_PATH_VALUE` | src/Utils.cpp | The PATH value the split cache was built from, the change-detection key. |
| `PATH_COMMAND_NAMES` | src/Utils.cpp | The flattened list of every command name across PATH for completion. |
| `PATH_COMMAND_NAMES_IS_VALID` | src/Utils.cpp | The validity flag for the name list. |
| `BUILT_PATH_DIRS` | src/Utils.cpp | The PATH directories the command-name cache was built against. |
| `BUILT_PATH_DIRS_IS_VALID` | src/Utils.cpp | The validity flag for the built directory list. |

## Source line indices

Each index is keyed by the source data pointer and length and is rebuilt when
the source changes. One source is active at a time.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `LINE_NUMBER_CACHE` | src/Utils.cpp | The memoized newline-offset index for the current source, so a line number is a binary search. |
| `SOURCE_LINE_INDEX` | src/Errors.cpp | The located-caret line and codepoint index used to render an error caret. It is cleared through `invalidate_source_line_index`. |

## Completion caches and arenas

Each cache is per-session. An arena is reset at the top of its pass, and the
results are deep-copied to the heap at the escape boundary before the arena is
reused. A directory listing is keyed by path and invalidated by mtime.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `COMPLETION_ARENA` | src/Completion.cpp | The mark and release arena backing every allocation of one TAB completion pass. It is reset at the top of each completion. |
| `DIRECTORY_LISTING_CACHE` | src/Completion.cpp | A four-slot most-recently-used cache of typed directory listings keyed by path, shared with the highlighter. |
| `HIGHLIGHT_ARENA` | src/CompletionHighlight.cpp | The arena backing one syntax-highlight render. The previous render stays valid until the editor drains it. |
| `CACHED_PATH_VERDICT_PATH` | src/CompletionHighlight.cpp | The PATH value the highlighter resolve verdicts were computed against. |
| `PATH_SEARCH_VERDICTS` | src/CompletionHighlight.cpp | A first-word command name mapped to whether it resolves, flushed when the PATH changes. |
| `BUILD_TARGET_CACHE` | src/CompletionScan.cpp | A build file mapped to the targets scanned from it. |
| `MANPAGE_OPTION_CACHE` | src/CompletionManpage.cpp | A command mapped to the options parsed from its manpage. An empty list is cached so a command with no manpage is not retried. |
| `MAN_SUBCOMMAND_INDEX` | src/CompletionManpage.cpp | A command mapped to its manpage subcommand names. |
| `MAN_PAGE_FILE_PATHS` | src/CompletionManpage.cpp | Every stripped section-1 page name mapped to its file path, the existence gate for the subcommand split. |
| `MAN_SUBCOMMAND_PAGE_VALID` | src/CompletionManpage.cpp | The memoized existence check for a candidate subcommand manpage. |
| `is_man_subcommand_index_built` | src/CompletionManpage.cpp | The build-once latch for the three manpage index maps. |
| `HELP_OPTION_CACHE` | src/CompletionManpage.cpp | A command mapped to the options parsed from its --help output. |
| `HELP_SUBCOMMAND_CACHE` | src/CompletionManpage.cpp | A command mapped to the subcommands parsed from its --help output. |
| `HELP_PARSED` | src/CompletionManpage.cpp | The set of keys already --help-parsed, so a fork happens at most once per key. |

## Prompt caches

Each cache is memoized against its recorded inputs and is discarded when an
input changes. One interactive prompt renders at a time. An impure prompt
template never caches.

| Name | File | Purpose |
| ---- | ---- | ------- |
| `PROMPT_COMMAND_CACHED_TEXT` | src/Main.cpp | The PROMPT_COMMAND text the cached tree was parsed from. |
| `PROMPT_COMMAND_CACHED_AST` | src/Main.cpp | The parsed PROMPT_COMMAND tree reused while the hook text is unchanged. |
| `PROMPT_CACHE_TEMPLATE` | src/Toiletline.cpp | The previous PS1 template. |
| `PROMPT_CACHE_INPUTS` | src/Toiletline.cpp | The parameters the template read. |
| `PROMPT_CACHE_EXPANSION` | src/Toiletline.cpp | The rendered expansion reused while every input is unchanged. |
| `PROMPT_CACHE_VALID` | src/Toiletline.cpp | The validity flag for the prompt cache. |
| `TL_BUFFER` | src/Toiletline.cpp | The fixed editor input line buffer. One editor reads one line at a time. |

## One-shot name lists

Each list is built once from a static table on first use and is read-only after.

| Name | File | Purpose |
| ---- | ---- | ------- |
| names (`builtin_names`) | src/Builtin.cpp | The builtin names collected from the entry table. |
| names (`util_names`) | src/Shitbox.cpp | The shitbox utility names collected from the entry table. |
| names (`signal_names`) | src/Platform.cpp | The signal names collected from the signal pair table, one list per platform block. |
| option name and letter lists (`shell_option_names`, `shell_option_letters`) | src/builtins/Set.cpp | The set option names and their single-letter forms. |
| names (`shopt_option_name_list`) | src/builtins/Shopt.cpp | The shopt option names. |
| `CACHED_USER` (in `build_prompt`) | src/Toiletline.cpp | The user name resolved once for the prompt, since the user is stable for the session. |
| dash-flag candidates (`per_kind_candidates`, `binary_candidates`) | src/CompletionScan.cpp | The dash-flag completion candidates memoized per builtin kind, derived from static registrar flag data. |

## Diagnostic sequencing

| Name | File | Purpose |
| ---- | ---- | ------- |
| `MESSAGE_LEADING_NEWLINE_ARMED` | src/Cli.cpp | Whether the next diagnostic emits a leading newline to separate it from prior output. It is thread_local, so the state is per-thread. |
