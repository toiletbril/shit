import * as vscode from "vscode";
import {
  Executable,
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
} from "vscode-languageclient/node";

const SERVER_ARGUMENT = "--as-language-server";
const CONFIGURATION_SECTION = "kosh";

/*
 * Koshka reads a language identifier it does not recognize as a plain shell
 * script and analyzes the whole document that way. Only the identifiers it
 * compares against are listed here. A justfile and an RPM spec file are
 * recognized by their names. Both are matched by pattern.
 */
const DOCUMENT_SELECTOR = [
  { scheme: "file", language: "shellscript" },
  { scheme: "file", language: "kosh" },
  { scheme: "file", language: "shit" },
  { scheme: "file", language: "yaml" },
  { scheme: "file", language: "dockercompose" },
  { scheme: "file", language: "dockerfile" },
  { scheme: "file", language: "markdown" },
  { scheme: "file", language: "makefile" },
  { scheme: "file", language: "json" },
  { scheme: "file", language: "jsonc" },
  { scheme: "file", pattern: "**/{justfile,.justfile,Justfile}" },
  { scheme: "file", pattern: "**/*.spec" },
  { scheme: "untitled", language: "shellscript" },
  { scheme: "untitled", language: "kosh" },
  { scheme: "untitled", language: "shit" },
];

let client: LanguageClient | undefined;

function get_configuration(): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration(CONFIGURATION_SECTION);
}

/*
 * The server option is placed after the user arguments. The shell rejects it
 * next to a command string, a script operand, or another mode, so a mistaken
 * argument fails the startup instead of hanging it.
 */
function build_server_options(): ServerOptions {
  const configuration = get_configuration();
  const binary_path = configuration.get<string>("path", "kosh");
  const extra_arguments = configuration.get<string[]>("arguments", []);

  /*
   * An executable with no transport talks over the standard streams of the
   * child. The explicit stdio transport appends a `--stdio` flag, and the
   * shell rejects it.
   */
  const executable: Executable = {
    command: binary_path,
    args: [...extra_arguments, SERVER_ARGUMENT],
    options: { env: { ...process.env, NO_COLOR: "1" } },
  };

  return executable;
}

function build_client_options(): LanguageClientOptions {
  return {
    documentSelector: DOCUMENT_SELECTOR,
    diagnosticCollectionName: "kosh",
    outputChannelName: "Koshka",
  };
}

async function start_client(): Promise<void> {
  if (client !== undefined) {
    return;
  }

  if (!get_configuration().get<boolean>("enable", true)) {
    return;
  }

  client = new LanguageClient(
    "kosh",
    "Koshka",
    build_server_options(),
    build_client_options(),
  );

  await client.start();
}

async function stop_client(): Promise<void> {
  const running_client = client;
  client = undefined;

  if (running_client !== undefined) {
    await running_client.stop();
  }
}

/*
 * The server reads no configuration after the handshake, so a changed binary
 * path or argument list reaches it through a restart.
 */
async function restart_client(): Promise<void> {
  await stop_client();
  await start_client();
}

export async function activate(
  context: vscode.ExtensionContext,
): Promise<void> {
  context.subscriptions.push(
    vscode.commands.registerCommand("kosh.restartServer", restart_client),
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      const did_change =
        event.affectsConfiguration("kosh.path") ||
        event.affectsConfiguration("kosh.arguments") ||
        event.affectsConfiguration("kosh.enable");

      if (did_change) {
        await restart_client();
      }
    }),
  );

  await start_client();
}

export async function deactivate(): Promise<void> {
  await stop_client();
}
