use zed_extension_api::{
    self as zed, settings::LspSettings, Command, LanguageServerId, Result, Worktree,
};

const BINARY_NAME: &str = "kosh";
const SERVER_ARGUMENT: &str = "--as-language-server";

struct KoshExtension;

impl zed::Extension for KoshExtension {
    fn new() -> Self {
        Self
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
            None => worktree.which(BINARY_NAME).ok_or_else(|| {
                format!(
                    "{BINARY_NAME} was not found on the PATH. Install the Koshka shell, \
                     or name the binary under lsp.{BINARY_NAME}.binary.path in your settings."
                )
            })?,
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
