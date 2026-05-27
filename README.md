# Taskbar Media Player Native (Standalone)

Um reprodutor de mídia e leitor de letras nativo, transparente e elegante, que se integra de forma fluida à barra de tarefas do Windows 11 e Windows 10. 

Este projeto foi originalmente concebido como um mod para o Windhawk e agora foi transformado em um aplicativo nativo independente (*standalone*) de código aberto, ideal para ser distribuído diretamente na Microsoft Store.

---

## ✨ Funcionalidades

* **Integração Fluida com a Barra de Tarefas**: O widget se integra nativamente sobre a barra de tarefas do Windows (usando estilos DWM transparentes), ocupando zero de espaço adicional na sua área de trabalho.
* **Controles de Mídia Universais**: Funciona com qualquer reprodutor de mídia do Windows 10/11 que suporte a API GSMTC (Spotify, YouTube no navegador Chrome/Edge, Windows Media Player, Deezer, etc.).
* **Exibição Dinâmica de Letras Sincronizadas**:
  - Busca automática de letras via `lrclib.net`, `NetEase Cloud Music`, `Vagalume` e `lyrics.ovh`.
  - Tradução instantânea de letras para o idioma do seu sistema utilizando a API do Google Translate.
  - Rolagem automática sincronizada com o progresso da música ou rolagem manual por scroll do mouse.
* **Barra de Progresso e Controle de Volume**:
  - Clique e arraste na barra de progresso do painel para navegar pela música (*seek*).
  - Controle de volume dedicado para o aplicativo de música em reprodução ou do sistema geral através da barra deslizante ou do scroll do mouse.
* **Modo Silencioso (Bandeja do Sistema)**: Funciona inteiramente em segundo plano. Um ícone na bandeja do sistema (*System Tray*) permite pausar, pular faixas, abrir configurações e fechar o aplicativo.
* **Ocultação Automática (Full Screen)**: Desaparece automaticamente ao rodar aplicativos ou jogos em tela cheia para evitar distrações.
* **Sincronização de Tema**: Adapta-se automaticamente ao tema Escuro (*Dark Mode*) ou Claro (*Light Mode*) do Windows.

---

## 🛠️ Como Usar

1. Execute o arquivo `TaskbarMediaPlayer.exe`.
2. O aplicativo criará um widget na sua barra de tarefas e exibirá um ícone na bandeja do sistema (ao lado do relógio).
3. **Controles Rápidos**:
   - **Clique no Widget**: Abre ou fecha o painel flutuante de letras e detalhes.
   - **Hover nos botões**: Exibe controles de Anterior, Play/Pause e Próximo.
   - **Scroll do mouse sobre o widget**: Ajusta o volume geral do sistema.
   - **Clique com botão direito no ícone da bandeja**: Dá acesso rápido aos controles de mídia, ativa inicialização com o Windows, abre as configurações e permite fechar o app.

### ⚙️ Configurações

O aplicativo salva suas preferências em formato JSON na pasta `%APPDATA%\TaskbarMediaPlayer\config.json`. Ao clicar em **Configurações** na bandeja do sistema, este arquivo é aberto para edição.

Os parâmetros configuráveis são:
* `width`: Largura do widget na barra de tarefas (padrão: `300`).
* `height`: Altura do widget (padrão: `46`).
* `fontSize`: Tamanho da fonte dos textos (padrão: `11`).
* `buttonScale`: Escala dos ícones de controle (padrão: `1.0`).
* `hideFullscreen`: Ocultar quando houver app em tela cheia (padrão: `false`).
* `idleTimeout`: Segundos sem reprodução antes de ocultar o widget (padrão: `0` - desativado).
* `offsetX` e `offsetY`: Distância em pixels das margens esquerda e vertical da barra de tarefas.

---

## 💻 Como Compilar o Código

O projeto é escrito em **C++ nativo** utilizando o framework **C++/WinRT** para chamadas das APIs modernas do Windows.

### Requisitos
* Windows 10 (versão 1809 ou superior) ou Windows 11.
* **Visual Studio 2022** com as cargas de trabalho:
  - Desenvolvimento para desktop com C++
  - C++/WinRT (normalmente incluído no Windows SDK recente)

### Passos para Compilar:
1. Abra o arquivo de solução [TaskbarMediaPlayer.sln](file:///e:/projetos/taskbar%20media%20player%20-%20Windows/TaskbarMediaPlayer.sln) no Visual Studio 2022.
2. Mude o perfil de compilação para **Release** e a plataforma para **x64**.
3. Clique em **Compilar -> Compilar Solução** (ou pressione `Ctrl + Shift + B`).
4. O executável standalone será gerado na pasta `x64\Release\TaskbarMediaPlayer.exe`.

### Empacotamento para a Windows Store (MSIX)
Para empacotar o projeto como um aplicativo pronto para a Microsoft Store:
1. Clique com o botão direito na Solução no Gerenciador de Soluções -> **Adicionar -> Novo Projeto**.
2. Escolha **Projeto de Empacotamento de Aplicativo do Windows** (Windows Application Packaging Project).
3. Adicione o projeto `TaskbarMediaPlayer` como referência de aplicativo no projeto de empacotamento.
4. Gere o pacote MSIX através do menu **Extensões de Projeto -> Publicar -> Criar Pacotes de Aplicativos**.

---

## 📄 Licença

Este projeto é distribuído sob a Licença MIT. Consulte o arquivo [LICENSE](file:///e:/projetos/taskbar%20media%20player%20-%20Windows/LICENSE) para mais detalhes.
