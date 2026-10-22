# GravaçãoTela

Gravador de tela leve para Linux Mint XFCE, pensado para computadores modestos (como Intel i5 antigo e 4 GB de RAM).

## O que tem

- Interface gráfica nativa GTK 3, sem Electron nem navegador embutido.
- Captura da tela inteira ou de uma região retangular escolhida com o mouse.
- 10, 15 ou 30 FPS.
- Cursor incluído ou oculto.
- MP4 com vídeo H.264 (`libx264`), `ultrafast` e `yuv420p`.
- Perfil de compressão para arquivo menor, equilibrado ou mais nítido.
- Áudio do sistema opcional via PulseAudio/PipeWire (captura a saída padrão, não o microfone).
- Ao parar, envia o comando de finalização ao FFmpeg para encerrar corretamente o arquivo MP4.

A gravação fica no próprio computador; o aplicativo não envia arquivos para a internet.

## Instalar no Linux Mint 22.x XFCE

Na pasta do repositório, execute:

```bash
chmod +x install.sh
./install.sh
```

O instalador pede autorização para instalar dependências do sistema com `apt`, compila o programa e cria um atalho no menu de aplicativos. Para iniciar pelo terminal:

```bash
~/.local/bin/gravacaotela
```

Dependências principais: GTK 3, FFmpeg, `slop` (seletor de região), `xdg-open` e `pactl` para áudio opcional.

## Compilar manualmente

```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev ffmpeg slop xdg-utils pulseaudio-utils
make
./build/gravacaotela
```

`make dist` cria `dist/gravacaotela-linux-x86_64.tar.gz`, com o executável e este README. O workflow do GitHub Actions também gera esse pacote como artefato a cada push. O binário usa bibliotecas GTK do sistema, então não é totalmente independente das dependências Linux.

## Sugestões para o seu computador

Comece com **15 FPS**, compressão **Equilibrado (CRF 27)** e a captura apenas da região necessária. Se a gravação ficar lenta, use **10 FPS** e **Arquivo menor (CRF 30)**. Para vídeo mais fluido, tente 30 FPS; isso aumenta o trabalho de captura e codificação.

A captura usa o display **X11**, comum no XFCE. Esta versão não implementa captura nativa do Wayland. Se a seleção de região não abrir, confirme que `slop` está instalado e que a sessão gráfica é X11.

## Áudio

A opção de áudio grava o monitor da saída de áudio padrão configurada no PulseAudio/PipeWire. O áudio fica desligado por padrão para reduzir carga e evitar gravar som sem querer. Para usar a opção, a ferramenta `pactl` precisa conseguir consultar a saída padrão.

## Estado do projeto

Primeira versão em desenvolvimento. O repositório inclui compilação automatizada no GitHub Actions, mas o comportamento de captura precisa ser validado na sessão XFCE/X11 da máquina de destino.