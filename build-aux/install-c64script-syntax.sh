#!/bin/bash
# Install C64Script syntax highlighting for VS Code

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOC_DIR="$SCRIPT_DIR/../doc/c64script/vscode"
VSCODE_EXT_DIR="$HOME/.vscode/extensions/c64script-syntax-0.1.0"

echo "🔧 Installing C64Script syntax highlighting for VS Code..."

# Create extension directory
mkdir -p "$VSCODE_EXT_DIR"

# Copy files
echo "📦 Copying language files..."
cp "$DOC_DIR/c64script.tmLanguage.json" "$VSCODE_EXT_DIR/"
cp "$DOC_DIR/c64script-language-configuration.json" "$VSCODE_EXT_DIR/"
cp "$DOC_DIR/c64script-vscode-package.json" "$VSCODE_EXT_DIR/package.json"

echo "✅ C64Script syntax highlighting installed!"
echo ""
echo "Next steps:"
echo "1. Reload VS Code window (Ctrl+Shift+P → 'Reload Window')"
echo "2. Open any .c64script file"
echo "3. Check language mode in bottom-right corner (should show 'C64Script')"
echo ""
echo "If syntax highlighting doesn't appear:"
echo "- Click the language mode indicator in bottom-right"
echo "- Select 'Configure File Association for .c64script...'"
echo "- Choose 'C64Script'"
