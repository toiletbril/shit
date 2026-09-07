use zed_extension_api::{
    self as zed, settings::LspSettings, Architecture, Command, DownloadedFileType,
    GithubReleaseOptions, LanguageServerId, LanguageServerInstallationStatus, Os, Result, Worktree,
};

const BINARY_NAME: &str = "kosh";
const SERVER_ARGUMENT: &str = "--as-language-server";
const RELEASE_REPOSITORY: &str = "toiletbril/kosh";

struct KoshExtension {
    downloaded_binary_path: Option<String>,
}

/*
 * An asset name holds the platform and the processor. The Darwin asset names
 * the processor aarch64, and the Linux and Windows assets name it amd64. No
 * asset is built for a 32-bit machine.
 */
fn get_asset_name_prefixes() -> Vec<String> {
    let (os, architecture) = zed::current_platform();

    let platform_name = match os {
        Os::Mac => "darwin",
        Os::Linux => "linux",
        Os::Windows => "win32",
    };

    let architecture_names: &[&str] = match architecture {
        Architecture::Aarch64 => &["aarch64", "arm64"],
        Architecture::X8664 => &["amd64", "x86_64"],
        Architecture::X86 => &[],
    };

    architecture_names
        .iter()
        .map(|name| format!("{BINARY_NAME}-{platform_name}-{name}-"))
        .collect()
}

fn get_binary_file_name() -> &'static str {
    match zed::current_platform().0 {
        Os::Windows => "kosh.exe",
        _ => BINARY_NAME,
    }
}

/*
 * Each download lives in a directory named after its release. Earlier ones are
 * removed once the new download is in place.
 */
fn remove_earlier_downloads(current_directory: &str) {
    let entries = match std::fs::read_dir(".") {
        Ok(entries) => entries,
        Err(_) => return,
    };

    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        let is_earlier_download =
            name.starts_with(&format!("{BINARY_NAME}-")) && name != current_directory;

        if is_earlier_download {
            let _ = std::fs::remove_dir_all(entry.path());
        }
    }
}

impl KoshExtension {
    /*
     * The shell is downloaded from the newest release. Prereleases are skipped.
     */
    fn download_binary(&mut self, language_server_id: &LanguageServerId) -> Result<String> {
        if let Some(path) = &self.downloaded_binary_path {
            if std::fs::metadata(path).is_ok() {
                return Ok(path.clone());
            }
        }

        zed::set_language_server_installation_status(
            language_server_id,
            &LanguageServerInstallationStatus::CheckingForUpdate,
        );

        let release = zed::latest_github_release(
            RELEASE_REPOSITORY,
            GithubReleaseOptions {
                require_assets: true,
                pre_release: false,
            },
        )?;

        let prefixes = get_asset_name_prefixes();

        let asset = prefixes
            .iter()
            .find_map(|prefix| {
                release
                    .assets
                    .iter()
                    .find(|asset| asset.name.starts_with(prefix))
            })
            .ok_or_else(|| {
                format!(
                    "Release {} has no {BINARY_NAME} binary for this platform. \
                     Build the shell from source and put it on your PATH.",
                    release.version
                )
            })?;

        let version_directory = format!("{BINARY_NAME}-{}", release.version);
        let binary_path = format!("{version_directory}/{}", get_binary_file_name());

        if std::fs::metadata(&binary_path).is_err() {
            zed::set_language_server_installation_status(
                language_server_id,
                &LanguageServerInstallationStatus::Downloading,
            );

            std::fs::create_dir_all(&version_directory)
                .map_err(|error| format!("The download directory was not created. {error}"))?;

            zed::download_file(
                &asset.download_url,
                &binary_path,
                DownloadedFileType::Uncompressed,
            )?;
            zed::make_file_executable(&binary_path)?;

            remove_earlier_downloads(&version_directory);
        }

        zed::set_language_server_installation_status(
            language_server_id,
            &LanguageServerInstallationStatus::None,
        );

        self.downloaded_binary_path = Some(binary_path.clone());

        Ok(binary_path)
    }
}

impl zed::Extension for KoshExtension {
    fn new() -> Self {
        Self {
            downloaded_binary_path: None,
        }
    }

    fn language_server_command(
        &mut self,
        language_server_id: &LanguageServerId,
        worktree: &Worktree,
    ) -> Result<Command> {
        let binary_settings = LspSettings::for_worktree(language_server_id.as_ref(), worktree)
            .ok()
            .and_then(|settings| settings.binary);

        let configured_path = binary_settings
            .as_ref()
            .and_then(|binary| binary.path.clone());

        let command = match configured_path {
            Some(path) => path,
            None => match worktree.which(BINARY_NAME) {
                Some(path) => path,
                None => self.download_binary(language_server_id).map_err(|error| {
                    zed::set_language_server_installation_status(
                        language_server_id,
                        &LanguageServerInstallationStatus::Failed(error.clone()),
                    );

                    format!(
                        "{BINARY_NAME} was not found on the PATH, and the release download \
                         failed. {error}"
                    )
                })?,
            },
        };

        let mut args = binary_settings
            .and_then(|binary| binary.arguments)
            .unwrap_or_default();

        if !args.iter().any(|argument| argument == SERVER_ARGUMENT) {
            args.push(SERVER_ARGUMENT.to_string());
        }

        Ok(Command {
            command,
            args,
            env: worktree.shell_env(),
        })
    }
}

zed::register_extension!(KoshExtension);
