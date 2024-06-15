# Shit

Man, my shell is shit™

Revolutionary command-line interpreter, or an interactive shell based on
[toiletline](https://github.com/toiletbril/toiletline) written in C++17.

This software was made as a late april fools joke and literally everything is
written from scratch. There's no guarantee that this project will come any close
to being finished, so use it at your own risk.

The goal I initially had in mind is to make a native, interactive,
`sh`-compatible shell that can be used interchangeably on **Windows** and Linux,
to provide a good `sh` experience on Windows without having to tolerate
PowerShell 💩, preferrably being faster (which seems very trivial). Later goals
were to offer basic replacements of most common coreutils commands like `mkdir`,
`rm`, `cat` and others as shell builtins.

## Development

`staging` is the development branch. It may be broken at any time. `master` is
more stable and should usually pass all tests.

You need a C++17 compatible compiler (but currently only Clang is supported for
Windows), GNU Make, `rm`, `mkdir` to build the executable; `diff` to run tests;
`clang-format` (better 18 or newer) to format code.

The `MODE` variable controls build type:
* `rel` is the optimized build;
* `dbg` includes all symbols and Asan with Ubsan;
* `cosmo` is an optimized build which will try to use `cosmoc++` from the
  Cosmopolitan toolchain.

An example of the build process:
```bash
$ export MODE=rel/dbg/cosmo
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
- [ ] Shell expansion. (`*`, `~`, `!`)
- [ ] Environment variables.
- [ ] Numeric expressions.

Is it good?
- [ ] Background jobs.
- [ ] Scripting capabilities. (flow control keywords)
- [ ] Complex scripting capabilites.

Is is exceptional? 
- [ ] Arbitrary precision numeric expressions.
- [ ] Cross-platform replacement for most common Unix programs which Windows
      does not have.
