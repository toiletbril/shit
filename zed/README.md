# Koshka for Zed

This extension runs the Koshka language server and routes formatting through
it. The server ships inside the shell binary, so the only requirement is a
`kosh` on your PATH.

No tree-sitter grammar is bundled. The server is attached to languages Zed
already provides.

## Installing

```bash
rustup target add wasm32-wasip2
```

Open the command palette, run `zed: install dev extension`, and select this
directory. Zed builds the extension and reloads it.

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
Script language, which is what puts them in front of the server.

The `"..."` entry keeps the other servers registered for the language. A list
without it replaces the defaults. Write `"!bash-language-server"` to drop a
server you do not want.

Keep `format_on_save` at `on`. The server formats whole documents and
advertises no range formatting, so the `modifications` mode would quietly skip
the file.

The named form of the formatter matters once a second server is attached,
because the bare `"language_server"` value picks the first one that can
format.

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

The language server option is appended when it is absent, so a `binary`
setting that lists other arguments still starts a working server.

## The command line formatter

`kosh --format` formats a file without the server, for a setup that runs no
language server at all.

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

Zed feeds the buffer through standard input, and a document with no file name
is read as a plain shell script. Configure this for shell languages only. A
YAML or Markdown buffer sent through it would be rewritten as shell.
