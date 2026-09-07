# Koshka for VS Code

This extension runs the Koshka language server. The same server formats the
document. The server is part of the shell binary. A `kosh` on your PATH is used
when there is one, and the latest release is offered for download when there is
not.

## Building and installing

```bash
npm install
npm run compile
npm run package
code --install-extension kosh.vsix
```

VSCodium names the same command `codium`.

## Finding the shell

The binary named by `kosh.path` is used as it is written. Otherwise `kosh` is
looked up on your PATH, then in the global storage of the extension.

When there is neither, a notification offers to download the latest release.
The asset for your platform is saved to the global storage, made executable,
and used to start the server. The
`Koshka: Download the shell from the latest release` command downloads it again
at any time.

There is no release asset for an Intel Mac. That machine builds the shell from
source.

## What is analyzed

Every host format the shell recognizes is analyzed with no setting. These are
shell scripts, YAML, Docker Compose, Dockerfiles, Markdown, makefiles, JSON,
and JSONC. A justfile and an RPM spec file are matched by name. Semantic
highlighting is on for all of them.

Formatting is offered for the same set. Koshka is the default formatter for
shell documents only. A YAML or Markdown document keeps its own formatter, and
`Format Document With...` picks Koshka for a single run.

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
