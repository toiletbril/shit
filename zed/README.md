# Koshka for Zed

This extension runs the Koshka language server. The same server formats the
document. The server is part of the shell binary. A `kosh` on your PATH is used
when one is there, and the latest release is downloaded when it is not.

No tree-sitter grammar is bundled. The server is attached to languages Zed
already provides.

## Installing

```bash
rustup target add wasm32-wasip2
```

Open the command palette, run `zed: install dev extension`, and select this
directory. Zed builds the extension and reloads it.

Zed reads its PATH from a login shell. A rustup toolchain under `~/.cargo/bin`
reaches the build only when a login startup file adds it. Bash reads
`~/.bash_profile` and falls back to `~/.profile` only when that file is absent.
Zsh reads `~/.zprofile`. Restart Zed after changing one of them.

## Settings

```jsonc
{
  "file_types": {
    "Shell Script": ["kosh", "shit"]
  },
  "languages": {
    "Shell Script": {
      "language_servers": ["kosh", "..."],
      "formatter": { "language_server": { "name": "kosh" } },
      "format_on_save": "on"
    },
    "YAML": { "language_servers": ["kosh", "..."] },
    "Markdown": { "language_servers": ["kosh", "..."] },
    "Dockerfile": { "language_servers": ["kosh", "..."] },
    "Docker Compose": { "language_servers": ["kosh", "..."] },
    "Make": { "language_servers": ["kosh", "..."] },
    "JSON": { "language_servers": ["kosh", "..."] },
    "JSONC": { "language_servers": ["kosh", "..."] }
  }
}
```

The `file_types` block gives `.kosh` and `.shit` files the built-in Shell
Script language. The server is attached to that language.

The `"..."` entry keeps the other servers registered for the language. A list
without it replaces the defaults. Write `"!bash-language-server"` to drop a
server you do not want.

Keep `format_on_save` at `on`. The server formats whole documents and
advertises no range formatting. The `modifications` mode skips the file and
reports nothing.

The bare `"language_server"` value picks the first attached server that can
format. The named form selects `kosh` once a second server is attached.

## Finding the shell

The binary named under `lsp.kosh.binary.path` is used as it is written.
Otherwise `kosh` is looked up on the PATH of the worktree.

When there is neither, the newest release is downloaded. The asset for your
platform is saved to a directory named after the release, made executable, and
used to start the server. Zed shows the progress in its language server status,
and earlier downloads are removed.

There is no release asset for an Intel Mac. That machine builds the shell from
source.

## Naming the binary

```jsonc
{
  "lsp": {
    "kosh": {
      "binary": {
        "path": "/usr/local/bin/kosh"
      }
    }
  }
}
```

The language server option is appended when it is absent. A `binary` setting
that lists other arguments still starts the server.

## The command line formatter

`kosh --format` formats a file without the language server.

```jsonc
{
  "languages": {
    "Shell Script": {
      "formatter": {
        "external": {
          "command": "kosh",
          "arguments": ["--format"]
        }
      }
    }
  }
}
```

Zed writes the buffer to standard input, and a document with no file name is
read as a plain shell script. Configure this for shell languages only. A YAML
or Markdown buffer would be rewritten as shell.
