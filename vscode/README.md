# Koshka for VS Code

This extension runs the Koshka language server. The same server formats the
document. The server is part of the shell binary, and a `kosh` on your PATH is
the only requirement.

## Building and installing

```bash
npm install
npm run compile
npm run package
code --install-extension kosh.vsix
```

VSCodium names the same command `codium`.

## What is analyzed

Every host format the shell recognizes is analyzed without any setting. The
list holds shell scripts, YAML, Docker Compose, Dockerfiles, Markdown,
makefiles, JSON, and JSONC. A justfile and an RPM spec file are matched by
name. Semantic highlighting is enabled for each of them.

Formatting is offered for the same set. Koshka is the default formatter for
shell documents alone. A YAML or Markdown document keeps the formatter it
already has, and `Format Document With...` picks Koshka for a single run.

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
