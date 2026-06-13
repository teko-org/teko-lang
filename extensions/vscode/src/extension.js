const vscode = require('vscode');
const cp = require('child_process');
const path = require('path');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

// Dynamically resolves the Teko compiler path in a flexible way
function resolveCompilerPath(context) {
    // Level 1: Attempts to read a custom setting from VS Code itself (set by the user)
    const configPath = vscode.workspace.getConfiguration('teko').get('compilerPath');
    if (configPath && configPath.trim() !== "") {
        return configPath;
    }

    // Level 2: If the extension was packaged with an embedded binary (Store Distribution / GitHub Releases)
    // The binary would be at: extensions/vscode/bin/teko
    const embeddedPath = path.join(context.extensionPath, 'bin', process.platform === 'win32' ? 'teko.exe' : 'teko');
    // For simplicity, without a heavy synchronous fs check, we return it as a viable fallback

    // Level 3: Industry Standard - Assumes the executable is added to the OS environment variables (PATH)
    // Enabling global calls as simply 'teko'
    return "teko";
}

function activate(context) {
    console.log('Teko dynamic extension activated successfully!');

    // Resolves the path dynamically without hard-coding fixed strings
    const compilerPath = resolveCompilerPath(context);

    // 1. LANGUAGE SERVER SETUP AND ALLOCATION (LSP CLIENT)
    const serverOptions = {
        run: { command: compilerPath, args: ["check"] },
        debug: { command: compilerPath, args: ["check"] }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'teko' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.tks')
        }
    };

    client = new LanguageClient('tekoLanguageServer', 'Teko Language Server', serverOptions, clientOptions);
    client.start();

    // 2. IDE COMMAND: Teko: Run Project (Run)
    let runCommand = vscode.commands.registerCommand('teko.run', function () {
        const folders = vscode.workspace.workspaceFolders;
        if (!folders) {
            vscode.window.showErrorMessage('Open a folder containing a Teko project (.tkp) first.');
            return;
        }

        vscode.workspace.findFiles('**/*.tkp').then((files) => {
            if (files.length === 0) {
                vscode.window.showErrorMessage('Project manifest (<name>.tkp) not found.');
                return;
            }
            const tkpPath = files[0].fsPath;
            const outputChannel = vscode.window.createOutputChannel("Teko VM Output");
            outputChannel.show();

            cp.exec(`"${compilerPath}" run "${tkpPath}"`, (err, stdout, stderr) => {
                if (err) {
                    outputChannel.appendLine(`[Runtime Error]:\n${stderr}`);
                    return;
                }
                outputChannel.appendLine(stdout);
            });
        });
    });

    // 3. IDE COMMAND: Teko: Build Project (Build)
    let buildCommand = vscode.commands.registerCommand('teko.build', function () {
        const folders = vscode.workspace.workspaceFolders;
        if (!folders) return;

        vscode.workspace.findFiles('**/*.tkp').then((files) => {
            if (files.length === 0) return;
            const tkpPath = files[0].fsPath;

            cp.exec(`"${compilerPath}" build "${tkpPath}"`, (err, stdout, stderr) => {
                if (err) {
                    vscode.window.showErrorMessage('Catastrophic failure while compiling the project.');
                    return;
                }
                vscode.window.showInformationMessage('Teko artifact generated successfully in the directory!');
            });
        });
    });

    context.subscriptions.push(runCommand, buildCommand);
}

function deactivate() {
    if (!client) return undefined;
    return client.stop();
}

module.exports = { activate, deactivate };
