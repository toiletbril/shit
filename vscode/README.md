# Koshka for VS Code

This extension runs the Koshka language server and routes formatting through
it. The server ships inside the shell binary, so the only requirement is a
`kosh` on your PATH.

## Building and installing

```bash
npm install
npm run compile
npm run package
code --install-extension kosh.vsix
```

## What you get

Diagnostics, quick fixes, completion, hover, go to definition, a document
outline, rename, and semantic highlighting arrive from the server. The fix-all
action is registered as `source.fixAll.kosh`.

Formatting is provided by the server as well. The extension declares no
formatter of its own.

The extension attaches to shell files and to the host formats that carry
embedded shell, which are YAML, Docker Compose, Dockerfiles, Markdown,
Makefiles, JSON, justfiles, and RPM spec files. Language features work only
inside the embedded shell regions.

## Formatting on save

Formatting on save is a user setting and the extension does not turn it on for
you.

```jsonc
{
  "[shellscript]": {
    "editor.defaultFormatter": "toiletbril.kosh",
    "editor.formatOnSave": true
  }
}
```

Leave `editor.formatOnSaveMode` at its default value of `file`. The server
formats whole documents and advertises no range formatting, so the
`modifications` mode would skip the file in silence.

The `[kosh]` and `[shit]` languages already default to this extension as their
formatter, since no other extension claims them.

## Settings

| Setting | Meaning |
| ------- | ------- |
| `kosh.enable` | Runs the language server. |
| `kosh.path` | Names the shell binary, `kosh` by default. |
| `kosh.arguments` | Adds arguments before the language server option. |
| `kosh.trace.server` | Traces the messages exchanged with the server. |

The server reads its configuration once during the handshake, so a change to
the path, the arguments, or the enable switch restarts it.

## The command line formatter

`kosh --format` formats a file without the server. It is the fallback for a
setup that does not run a language server at all.

```bash
kosh --format script.sh
kosh --format --apply script.sh
```

Reading standard input gives the shell no file name, and a document with no
file name is read as a plain shell script. Pipe shell scripts only. A
Dockerfile or a workflow file has to be named on the command line for its
embedded regions to be found.
