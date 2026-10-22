#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEST_BIN="${HOME}/.local/bin/gravacaotela"
DESKTOP_DIR="${HOME}/.local/share/applications"

if ! command -v apt-get >/dev/null 2>&1; then
  echo "Este instalador foi preparado para Linux Mint/Ubuntu (apt)." >&2
  exit 1
fi

if [[ "${EUID}" -eq 0 ]]; then
  SUDO=()
else
  if ! command -v sudo >/dev/null 2>&1; then
    echo "Preciso do sudo para instalar as dependências do sistema." >&2
    exit 1
  fi
  SUDO=(sudo)
fi

echo "Instalando dependências de compilação e captura..."
"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y build-essential pkg-config libgtk-3-dev ffmpeg slop xdg-utils pulseaudio-utils

echo "Compilando GravaçãoTela..."
make -C "$ROOT_DIR"

echo "Instalando executável em $DEST_BIN"
install -D -m 0755 "$ROOT_DIR/build/gravacaotela" "$DEST_BIN"
mkdir -p "$DESKTOP_DIR"

cat > "$DESKTOP_DIR/gravacaotela.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=GravaçãoTela
Comment=Gravador de tela leve
Exec="$DEST_BIN"
Terminal=false
Categories=AudioVideo;Video;
EOF

echo
echo "Instalação concluída."
echo "Abra GravaçãoTela pelo menu de aplicativos ou execute:"
echo "  $DEST_BIN"
echo
echo "A captura funciona em sessões X11; no XFCE, confirme a sessão em uso se a captura falhar."
