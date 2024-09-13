# Shit

Man, my shell is shit™

Revolutionary command-line interpreter, or an interactive shell based on
[toiletline](https://github.com/toiletbril/toiletline) written in C++17.

This software was made as a late april fools joke and literally everything is
written from scratch. There's no guarantee that this project will come any close
to being finished, so use it at your own risk.

The goal is to be a native, interactive, `sh`-compatible shell without any bells
and whistles, that can be used interchangeably on Windows and Linux and offer a
good `sh` experience on Windows to avoid having to tolerate PowerShell or some
other fancy crossplatform shells, while being faster than all of them. Later
goal is to offer basic replacements of most common coreutils commands like
`mkdir`, `rm`, `cat` and others as shell builtins (and for the moment,
`busybox-w32` can be used instead).

## Development

`staging` is the development branch. It may be broken at any time. `master` is
more stable and should usually pass all tests.

You need a C++17 compatible compiler (but currently only Clang is supported for
Windows), GNU Make, `rm`, `mkdir` to build the executable; `cat`, `diff`,
`printf` to run the tests; `clang-format`, `clang-tidy` (better 18 or newer) to
check the code.

The `MODE` variable controls build type:
* `rel` is the optimized build;
* `prof` is optimized build with debug symbols for profiling;
* `dbg` includes all symbols and Asan with Ubsan;
* `cosmo` is an optimized build which will try to use `cosmoc++` from the
  Cosmopolitan toolchain.

An example of the build process:
```bash
$ export MODE=rel/prof/dbg/cosmo
$ make -j14
$ ./shit --help
```

## ...

Is it usable?
- [x] Run programs.
- [x] Work on Linux and Windows.
- [x] Logical sequences. (`&&`, `||`, `;`)
- [x] Pipes.
- [ ] Redirections. (`>`/`<`)
- [x] Shell expansion. (`?`, `[...]`, `*`, `~`)
- [x] Escaping.
- [ ] Environment variables.
- [ ] Numeric expressions.

Is it good?
- [ ] Background jobs.
- [ ] Scripting capabilities. (flow control keywords)
- [ ] Blocks and functions.
- [ ] `sh`-compatible.

Is is exceptional? 
- [ ] Complex scripting capabilites.
- [ ] Arbitrary precision numeric expressions.
- [ ] Cross-platform replacement for most common Unix programs which Windows
      does not have.
