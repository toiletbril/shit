# Koshka for VS Code

This extension runs the Koshka language server and formatter. The server is
part of the shell binary. A `kosh` on your PATH is used when there is one, and
the latest release is offered for download when there is not.

## Building and installing

```bash
$ npm install
$ npm run compile
$ npm run package
$ code --install-extension kosh.vsix
```

## Formatting on save

Formatting on save is a user setting. The extension does not enable it.

```jsonc
{
  "[shellscript]": {
    "editor.formatOnSave": true
  }
}
```

Leave `editor.formatOnSaveMode` at its default value of `file`. The server
formats whole documents and advertises no range formatting. The
`modifications` mode skips the file and reports nothing.

## Settings

| Setting | Meaning |
| ------- | ------- |
| `kosh.enable` | Starts the language server. |
| `kosh.path` | Names the shell binary. The default is `kosh`. |
| `kosh.arguments` | Adds arguments before the language server option. |
| `kosh.trace.server` | Traces the messages exchanged with the server. |

The server reads its configuration once at startup. A change to the path, the
arguments, or the enable switch restarts it.
