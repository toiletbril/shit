--
-- This snippet allows you to use Koshka's shell language server and its
-- formatter in Neovim 0.11+. Paste the code below into your configuration and
-- you should be good to go.
--
-- The server formats the document itself, so the formatter needs no separate
-- program. A recipe for the standalone `kosh --format` command is commented
-- out at the bottom.
--

-- start koshka snippet
vim.filetype.add({
  extension = {
    sh = "sh",
    bash = "bash",
    kosh = "kosh",
    shit = "shit",
  },
})
vim.treesitter.language.register("bash", {
  "sh",
  "bash",
  "kosh",
  "shit",
})

-- Format the buffer with the server every time it is written. Set this to
-- false to format only on demand with vim.lsp.buf.format().
local should_format_on_save = true

-- Koshka reads embedded shell out of a justfile by its file name. A buffer
-- named foo.just is a justfile to Neovim and an ordinary shell script to the
-- server, so the server is kept away from it.
local function is_analyzable_buffer(bufnr)
  if vim.bo[bufnr].filetype ~= "just" then
    return true
  end

  local name = vim.fs.basename(vim.api.nvim_buf_get_name(bufnr)):lower()
  return name == "justfile" or name == ".justfile"
end

vim.lsp.config("kosh", {
  cmd = { "kosh", "--as-language-server" },
  filetypes = {
    "sh",
    "bash",
    "kosh",
    "shit",
    "yaml",
    "yaml.ansible",
    "markdown",
    "dockerfile",
    "make",
    "json",
    "jsonc",
    "just",
    "spec",
  },
  root_dir = function(bufnr, on_dir)
    if not is_analyzable_buffer(bufnr) then
      return
    end

    on_dir(vim.fs.root(bufnr, { ".git" })
      or vim.fs.root(bufnr, { "Makefile" })
      or vim.fn.getcwd())
  end,
})
vim.lsp.enable("kosh")

vim.api.nvim_create_autocmd("LspAttach", {
  group = vim.api.nvim_create_augroup("kosh.format", { clear = true }),
  callback = function(event)
    local client = vim.lsp.get_client_by_id(event.data.client_id)
    if client == nil or client.name ~= "kosh" or not should_format_on_save then
      return
    end

    if not client:supports_method("textDocument/formatting") then
      return
    end

    vim.api.nvim_create_autocmd("BufWritePre", {
      group = vim.api.nvim_create_augroup("kosh.format." .. event.buf, { clear = true }),
      buffer = event.buf,
      callback = function()
        vim.lsp.buf.format({ bufnr = event.buf, id = client.id, timeout_ms = 2000 })
      end,
    })
  end,
})
-- end koshka snippet

--
-- The recipe below formats through the `kosh --format` command with
-- conform.nvim, for people who do not run the language server. Run one or the
-- other, never both.
--
-- Standard input carries no file name, so the command reads every buffer as a
-- shell script. Only shell filetypes are listed here. Sending a yaml,
-- markdown, dockerfile, make, json, just, or spec buffer through it would
-- rewrite the whole file as shell.
--
-- The mood is left unset. The default mood accepts every construct the other
-- moods accept, and a recognized shebang still selects the dialect.
--
-- require("conform").setup({
--   formatters = {
--     kosh = {
--       command = "kosh",
--       args = { "--format" },
--       stdin = true,
--       exit_codes = { 0 },
--       env = { NO_COLOR = "1" },
--     },
--   },
--   formatters_by_ft = {
--     sh = { "kosh" },
--     bash = { "kosh" },
--     kosh = { "kosh" },
--     shit = { "kosh" },
--   },
--   format_on_save = { timeout_ms = 2000, lsp_format = "never" },
-- })
