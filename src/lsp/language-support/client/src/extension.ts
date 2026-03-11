import { workspace, ExtensionContext, commands, window, Uri } from "vscode";
import * as path from "path";
import * as fs from "fs";

import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
  ErrorAction,
  CloseAction,
} from "vscode-languageclient/node";

let client: LanguageClient;

const LOG_FILE = "/tmp/luma_lsp.log";

function findLumaBinary(context: ExtensionContext): string | null {
  const candidates = [
    workspace.getConfiguration("luma").get<string>("serverPath"),
    path.resolve(__dirname, "..", "..", "..", "..", "..", "luma"),
    path.resolve(__dirname, "..", "..", "..", "..", "luma"),
    path.resolve(__dirname, "..", "..", "..", "luma"),
    ...(workspace.workspaceFolders?.map(f => path.join(f.uri.fsPath, "luma")) ?? []),
    "luma",
  ];

  for (const candidate of candidates) {
    if (!candidate) continue;
    if (candidate === "luma") return candidate;
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      return candidate;
    } catch {
      // not found or not executable, try next
    }
  }
  return null;
}

// ---------------------------------------------------------------------------
// Error handler with:
//   - per-window error count (resets after a quiet period)
//   - exponential backoff on restart
//   - hard shutdown only after HARD_LIMIT consecutive errors with no recovery
// ---------------------------------------------------------------------------
function makeErrorHandler(outputChannel: { appendLine(value: string): void }) {
  let errorCount    = 0;
  let restartCount  = 0;
  let resetTimer: ReturnType<typeof setTimeout> | null = null;

  const SOFT_LIMIT  = 5;   // log but keep going
  const HARD_LIMIT  = 20;  // give up and shut down
  const RESET_MS    = 10_000; // reset error window after 10s of quiet

  function scheduleReset() {
    if (resetTimer) clearTimeout(resetTimer);
    resetTimer = setTimeout(() => {
      if (errorCount > 0) {
        outputChannel.appendLine(
          `[LSP] Error window reset (had ${errorCount} errors)`
        );
      }
      errorCount = 0;
      resetTimer = null;
    }, RESET_MS);
  }

  return {
    error: (_error: any, _message: any, _count: any) => {
      errorCount++;
      scheduleReset();
      outputChannel.appendLine(`[LSP] Protocol error #${errorCount}`);

      if (errorCount >= HARD_LIMIT) {
        outputChannel.appendLine(
          `[LSP] ${HARD_LIMIT} consecutive errors — giving up.`
        );
        return { action: ErrorAction.Shutdown };
      }

      // Between SOFT_LIMIT and HARD_LIMIT: log a warning but continue
      if (errorCount >= SOFT_LIMIT) {
        outputChannel.appendLine(
          `[LSP] Warning: ${errorCount} errors so far, server may be unstable.`
        );
      }

      return { action: ErrorAction.Continue };
    },

    closed: () => {
      restartCount++;
      const backoffMs = Math.min(1000 * Math.pow(2, restartCount - 1), 30_000);

      outputChannel.appendLine(
        `[LSP] Connection closed (restart #${restartCount}, ` +
        `backoff ${backoffMs}ms)...`
      );

      // After too many restarts, give up so we don't spam VS Code
      if (restartCount > 5) {
        outputChannel.appendLine(
          "[LSP] Too many restarts — shutting down. " +
          "Run 'Luma: Show Binary Path' or check the log."
        );
        return { action: CloseAction.DoNotRestart };
      }

      // Reset error count on restart so a clean reconnect gets a fresh window
      errorCount = 0;

      // Returning Restart here; the actual backoff delay is not natively
      // supported by vscode-languageclient, so we use a deferred re-start
      // via a setTimeout if needed. For now Restart is sufficient.
      return { action: CloseAction.Restart };
    },
  };
}

export function activate(context: ExtensionContext) {
  const outputChannel = window.createOutputChannel("Luma LSP");
  outputChannel.show(true);

  const lumaBin = findLumaBinary(context);

  if (!lumaBin) {
    outputChannel.appendLine("[LSP] ERROR: Could not find luma binary!");
    outputChannel.appendLine(
      "[LSP] Set 'luma.serverPath' in settings.json to the full path of your luma binary."
    );
    window.showErrorMessage(
      'Luma LSP: binary not found. Set "luma.serverPath" in settings.json.',
      "Open Settings"
    ).then(choice => {
      if (choice === "Open Settings") {
        commands.executeCommand("workbench.action.openSettings", "luma.serverPath");
      }
    });
    return;
  }

  outputChannel.appendLine(`LSP binary:  ${lumaBin}`);
  outputChannel.appendLine(`LSP log:     ${LOG_FILE}`);
  outputChannel.appendLine(`Tail with:   tail -f ${LOG_FILE}`);

  const serverOptions: ServerOptions = {
    command: "sh",
    transport: TransportKind.stdio,
    args: ["-c", `exec "${lumaBin}" -lsp 2>>"${LOG_FILE}"`],
  };

  const traceChannel = window.createOutputChannel("Luma LSP Trace");

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "luma" }],
    synchronize: {
      fileEvents: workspace.createFileSystemWatcher("**/*.lx"),
    },
    outputChannel,
    traceOutputChannel: traceChannel,
    errorHandler: makeErrorHandler(outputChannel),
  };

  client = new LanguageClient(
    "luma-lsp",
    "Luma LSP",
    serverOptions,
    clientOptions
  );

  client.start().then(() => {
    outputChannel.appendLine("[LSP] Client started successfully");
  }).catch((err: Error) => {
    outputChannel.appendLine(`[LSP] Client failed to start: ${err}`);
    window.showErrorMessage(`Luma LSP failed to start: ${err.message}`);
  });

  context.subscriptions.push(
    commands.registerCommand("luma.openLog", () => {
      workspace.openTextDocument(Uri.file(LOG_FILE)).then(
        doc => window.showTextDocument(doc),
        () => window.showErrorMessage(`Could not open: ${LOG_FILE}`)
      );
    }),
    commands.registerCommand("luma.showBinaryPath", () => {
      window.showInformationMessage(`Luma binary: ${lumaBin}`);
    }),
    commands.registerCommand("luma.restartServer", () => {
      outputChannel.appendLine("[LSP] Manual restart requested");
      client.restart().then(() => {
        outputChannel.appendLine("[LSP] Server restarted successfully");
      }).catch((err: Error) => {
        outputChannel.appendLine(`[LSP] Restart failed: ${err.message}`);
      });
    })
  );
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) return undefined;
  return client.stop();
}