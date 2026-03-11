return {
  -- Filetype mapping for .lx → luma
  {
    "AstroNvim/astrocore",
    opts = {
      filetypes = {
        extension = {
          lx = "luma",
        },
      },
    },
  },

  -- LSP setup for Luma
  {
    "neovim/nvim-lspconfig",
    opts = function(_, opts)
      local luma_path = vim.fn.expand("luma")
      if vim.fn.executable(luma_path) == 0 then
        vim.notify("Luma LSP not found at " .. luma_path, vim.log.levels.WARN)
        return opts
      end

      local lspconfig = require("lspconfig")
      local configs = require("lspconfig.configs")

      if not configs.luma then
        configs.luma = {
          default_config = {
            cmd = { luma_path, "-lsp" },
            filetypes = { "luma" },
            root_dir = function(fname)
              return lspconfig.util.find_git_ancestor(fname)
                or lspconfig.util.path.dirname(fname)
            end,
            single_file_support = true,
            on_attach = function(client, bufnr)
              vim.notify("Luma LSP attached to buffer " .. bufnr, vim.log.levels.INFO)
              if client.server_capabilities.semanticTokensProvider then
                vim.lsp.semantic_tokens.start(bufnr, client.id)
              end
            end,
            on_exit = function(code, signal, _)
              if code ~= 0 then
                vim.notify(
                  string.format("Luma LSP exited with code %d (signal %d)", code, signal),
                  vim.log.levels.ERROR
                )
              end
            end,
          },
        }
      end

      local capabilities = vim.lsp.protocol.make_client_capabilities()
      local has_cmp, cmp_nvim_lsp = pcall(require, "cmp_nvim_lsp")
      if has_cmp then
        capabilities = cmp_nvim_lsp.default_capabilities(capabilities)
      end

      capabilities.textDocument.semanticTokens = {
        dynamicRegistration = true,
        tokenTypes = {
          "namespace", "type", "typeParameter", "function", "method",
          "property", "variable", "parameter", "keyword", "modifier",
          "comment", "string", "number", "operator", "struct", "enum",
          "enumMember",
        },
        tokenModifiers = {
          "declaration", "definition", "readonly", "static", "defaultLibrary",
        },
        formats = { "relative" },
        requests = {
          full = true,
          range = false,
        },
        multilineTokenSupport = false,
        overlappingTokenSupport = false,
      }

      lspconfig.luma.setup({
        capabilities = capabilities,
      })

      return opts
    end,
  },
}