import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import {
  Executable,
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
} from "vscode-languageclient/node";

const SERVER_ARGUMENT = "--as-language-server";
const CONFIGURATION_SECTION = "kosh";
const DEFAULT_BINARY_NAME = "kosh";
const RELEASES_URL =
  "https://api.github.com/repos/toiletbril/kosh/releases?per_page=10";
const SKIP_PROMPT_KEY = "kosh.skipDownloadPrompt";

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

/*
 * An asset name holds the platform and the processor. The Darwin asset names
 * the processor aarch64, and the Linux and Windows assets name it amd64.
 */
const ARCHITECTURE_TOKENS: Record<string, string[]> = {
  x64: ["amd64", "x86_64"],
  arm64: ["aarch64", "arm64"],
};

interface ReleaseAsset {
  name: string;
  browser_download_url: string;
}

interface Release {
  tag_name: string;
  prerelease: boolean;
  draft: boolean;
  assets: ReleaseAsset[];
}

let client: LanguageClient | undefined;

function get_configuration(): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration(CONFIGURATION_SECTION);
}

function is_executable_file(candidate: string): boolean {
  try {
    fs.accessSync(candidate, fs.constants.X_OK);

    return fs.statSync(candidate).isFile();
  } catch {
    return false;
  }
}

function find_on_path(name: string): string | undefined {
  const search_path = process.env.PATH ?? "";
  const extensions =
    process.platform === "win32"
      ? (process.env.PATHEXT ?? ".EXE").split(path.delimiter)
      : [""];

  for (const directory of search_path.split(path.delimiter)) {
    if (directory.length === 0) {
      continue;
    }

    for (const extension of extensions) {
      const candidate = path.join(directory, name + extension);

      if (is_executable_file(candidate)) {
        return candidate;
      }
    }
  }

  return undefined;
}

function get_downloaded_binary_path(context: vscode.ExtensionContext): string {
  const name = process.platform === "win32" ? "kosh.exe" : "kosh";

  return path.join(context.globalStorageUri.fsPath, "bin", name);
}

/*
 * A configured path is passed on as it was written. The default name is looked
 * up on PATH, then in the storage of the extension.
 */
function resolve_binary_path(
  context: vscode.ExtensionContext,
): string | undefined {
  const configured_path = get_configuration().get<string>(
    "path",
    DEFAULT_BINARY_NAME,
  );

  if (configured_path !== DEFAULT_BINARY_NAME) {
    return configured_path;
  }

  const path_copy = find_on_path(DEFAULT_BINARY_NAME);

  if (path_copy !== undefined) {
    return path_copy;
  }

  const stored_copy = get_downloaded_binary_path(context);

  if (is_executable_file(stored_copy)) {
    return stored_copy;
  }

  return undefined;
}

function select_release_asset(
  releases: Release[],
): { tag: string; asset: ReleaseAsset } | undefined {
  const tokens = ARCHITECTURE_TOKENS[process.arch] ?? [process.arch];
  const prefixes = tokens.map(
    (token) => `${DEFAULT_BINARY_NAME}-${process.platform}-${token}-`,
  );

  for (const release of releases) {
    if (release.prerelease || release.draft) {
      continue;
    }

    for (const asset of release.assets ?? []) {
      const has_matching_name = prefixes.some((prefix) =>
        asset.name.startsWith(prefix),
      );

      if (has_matching_name) {
        return { tag: release.tag_name, asset };
      }
    }
  }

  return undefined;
}

/*
 * The release list is read, and the newest release with an asset for this
 * platform is used. Drafts and prereleases are skipped.
 */
async function download_release_binary(
  context: vscode.ExtensionContext,
  progress: vscode.Progress<{ message?: string }>,
): Promise<string> {
  progress.report({ message: "Reading the release list" });

  const releases_response = await fetch(RELEASES_URL, {
    headers: {
      Accept: "application/vnd.github+json",
      "User-Agent": "kosh-vscode",
    },
  });

  if (!releases_response.ok) {
    throw new Error(
      `The release list request failed with status ${releases_response.status}.`,
    );
  }

  const releases = (await releases_response.json()) as Release[];
  const selection = select_release_asset(releases);

  if (selection === undefined) {
    throw new Error(
      `No release has a binary for ${process.platform} ${process.arch}.`,
    );
  }

  progress.report({ message: `Downloading ${selection.asset.name}` });

  const asset_response = await fetch(selection.asset.browser_download_url, {
    headers: { "User-Agent": "kosh-vscode" },
  });

  if (!asset_response.ok) {
    throw new Error(
      `The download failed with status ${asset_response.status}.`,
    );
  }

  const payload = new Uint8Array(await asset_response.arrayBuffer());
  const binary_path = get_downloaded_binary_path(context);

  await fs.promises.mkdir(path.dirname(binary_path), { recursive: true });
  await fs.promises.writeFile(binary_path, payload);

  if (process.platform !== "win32") {
    await fs.promises.chmod(binary_path, 0o755);
  }

  return selection.tag;
}

async function download_and_restart(
  context: vscode.ExtensionContext,
): Promise<void> {
  await stop_client();

  try {
    const tag = await vscode.window.withProgress(
      { location: vscode.ProgressLocation.Notification, title: "Koshka" },
      (progress) => download_release_binary(context, progress),
    );

    vscode.window.showInformationMessage(
      `Koshka ${tag} was downloaded to ${get_downloaded_binary_path(context)}.`,
    );
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);

    vscode.window.showErrorMessage(`The Koshka download failed. ${detail}`);

    return;
  }

  await start_client(context);
}

async function offer_download(context: vscode.ExtensionContext): Promise<void> {
  if (context.globalState.get<boolean>(SKIP_PROMPT_KEY, false)) {
    return;
  }

  const download_action = "Download";
  const skip_action = "Never ask again";

  const selection = await vscode.window.showWarningMessage(
    "The kosh binary was not found on your PATH. The latest release can be" +
      " downloaded into the storage of the extension.",
    download_action,
    skip_action,
  );

  if (selection === download_action) {
    await download_and_restart(context);

    return;
  }

  if (selection === skip_action) {
    await context.globalState.update(SKIP_PROMPT_KEY, true);
  }
}

/*
 * The server option is placed after the user arguments. The shell rejects it
 * next to a command string, a script operand, or another mode, and a mistaken
 * argument fails the startup.
 */
function build_server_options(binary_path: string): ServerOptions {
  const extra_arguments = get_configuration().get<string[]>("arguments", []);

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

async function start_client(context: vscode.ExtensionContext): Promise<void> {
  if (client !== undefined) {
    return;
  }

  if (!get_configuration().get<boolean>("enable", true)) {
    return;
  }

  const binary_path = resolve_binary_path(context);

  if (binary_path === undefined) {
    await offer_download(context);

    return;
  }

  client = new LanguageClient(
    "kosh",
    "Koshka",
    build_server_options(binary_path),
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
 * The server reads no configuration after the handshake. A changed binary
 * path or argument list reaches it through a restart.
 */
async function restart_client(context: vscode.ExtensionContext): Promise<void> {
  await stop_client();
  await start_client(context);
}

export async function activate(
  context: vscode.ExtensionContext,
): Promise<void> {
  context.subscriptions.push(
    vscode.commands.registerCommand("kosh.restartServer", () =>
      restart_client(context),
    ),
  );

  context.subscriptions.push(
    vscode.commands.registerCommand("kosh.downloadServer", () =>
      download_and_restart(context),
    ),
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration(async (event) => {
      const did_change =
        event.affectsConfiguration("kosh.path") ||
        event.affectsConfiguration("kosh.arguments") ||
        event.affectsConfiguration("kosh.enable");

      if (did_change) {
        await restart_client(context);
      }
    }),
  );

  await start_client(context);
}

export async function deactivate(): Promise<void> {
  await stop_client();
}
