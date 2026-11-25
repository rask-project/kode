# Tekst — Roadmap

Editor de texto nativo para macOS em **Qt 6 / QML**, com código nativo
(Objective-C++/AppKit) só onde o Qt não entrega a experiência de um app Mac.

Este documento define **a ordem de implementação**. Cada etapa termina com um
app que compila, abre e é utilizável; nenhuma etapa depende de uma etapa
posterior.

---

## 0. Decisões de base

| Tema | Decisão | Motivo |
|---|---|---|
| Toolchain | C++23, Qt 6.12, CMake ≥ 3.21 + Ninja | Igual ao konsoll |
| Plataforma | macOS 14.4+, arm64 | Igual ao konsoll/metronom |
| Bundle id | `com.github.rask-project.tekst` (`.debug` no Debug) | Mesmo padrão do konsoll |
| Módulo QML do app | URI `Tekst` | |
| Biblioteca comum | novo repositório **`rask-project/rask-qml`**, módulos `Rask.*` | Ver seção 2 |
| Licença | MIT (Rask Project) | Igual aos demais repositórios |

---

## 1. O que já existe e pode ser reaproveitado

Levantamento dos repositórios irmãos (`../konsoll`, `../metronom`, `../notes`):

| Recurso | Onde está hoje | Observação |
|---|---|---|
| Janela translúcida + blur + conteúdo sob a title bar | `konsoll/src/MacOSTranslucency.mm`, `WindowTranslucency` · `metronom/Metronom/MacWindowEffects.mm`, `NativeWindow` · `notes/Rask/Controls/Native/MacOSWindow` | **Três implementações do mesmo recurso** → primeira coisa a unificar |
| Inset dos botões de semáforo (traffic lights) | `metronom NativeWindow.leadingInset` | Necessário para a tab strip |
| Duplo clique na title bar (zoom/minimizar conforme o sistema), janela movível | `konsoll WindowTranslucency` | |
| Liquid Glass (macOS 26, `NSGlassEffectView`) | `konsoll/src/GlassEffect.*`, `controls/GlassSurface.qml` | |
| Cores do sistema (`NSColor` semânticas) | `notes/.../UIKit.*` | Útil p/ cores de seleção, cursor, fundo de texto |
| Claro/escuro escolhido pelo usuário | `metronom/Metronom/Appearance.*` | |
| Diálogos nativos (`NSAlert`, prompt de texto) | `konsoll/src/NativeDialogs.mm` | Hoje com métodos específicos do terminal → generalizar |
| Painel "Sobre" nativo | `konsoll/src/AboutDialog.mm` | |
| Updates via Sparkle | `konsoll/src/Updater.mm` (completo) · `metronom/Metronom/MacUpdater.mm` (simples) · `cmake/Sparkle.cmake` + `tools/update-appcast.py` nos dois | |
| Estado/geometria de janelas, tela, restauração | `konsoll/src/WindowState*`, `WindowScreen` | |
| Quit guard (`NSApplicationDelegate`) e menu do Dock | `konsoll/src/AppQuitGuard.mm`, `DockMenu.mm` | O Tekst precisa do quit guard para documentos não salvos |
| Tab strip / tab button | `konsoll/controls/TerminalTabStrip.qml`, `TerminalTabButton.qml` | Acoplados ao terminal → extrair versão genérica |
| SearchBar, Menu, ToolButton, FormGroup/FormRow, SettingsSidebarItem | `konsoll/controls/` | |
| Estilo Qt Quick Controls para macOS | `metronom/MacStyle/` | Completa o que falta no estilo `macOS` do Qt |
| Versão a partir da tag git, `CFBundleVersion`, assinatura ad hoc, `PruneBundle`, ícones | `CMakeLists.txt` e `cmake/` dos dois apps | Copiado entre projetos → vira módulo CMake |

---

## 2. Biblioteca comum: `rask-qml`

Novo repositório `rask-project/rask-qml`, consumido pelos apps como
**submódulo git** (`external/rask-qml`) com `add_subdirectory`. Alternativa:
`FetchContent` fixado numa tag (fica para depois que a API estabilizar).

### Regras

- Cada módulo é uma **biblioteca QML estática** (`qt_add_library(... STATIC)` +
  `qt_add_qml_module`), como o `metronom_qml`. Os apps linkam o alvo
  `Rask::Window`, etc.
- Namespace C++ `rask`. Tipos registrados com `QML_ELEMENT` / `QML_SINGLETON`.
- **Multiplataforma por contrato**: o metronom roda em Android e Linux. Todo
  `.mm` tem um par `_stub.cpp` (no-op) selecionado no CMake, e cada tipo expõe
  `supported`/`translucent` para o QML ter um fallback.
- Qt mínimo **6.10** (o metronom exige 6.10; o konsoll e o Tekst usam 6.12).
- Sem nada específico de um app: nada de "workspace", "session", "agent".
- Cada módulo com seu exemplo em `examples/` e teste (`qmltestrunner` / QtTest).

### Módulos

| Módulo (URI) | Conteúdo | Origem |
|---|---|---|
| **`Rask.Platform`** | `Appearance` (claro/escuro), `SystemColors` (cores `NSColor` semânticas), `AppInfo` (versões, `nativeMenuBar`) | metronom, notes |
| **`Rask.Window`** | `NativeWindow` (translucidez, blur, conteúdo sob a title bar, `leadingInset`, movível, duplo clique na title bar, `representedFile`, `documentEdited`), `GlassEffect`, `WindowState` (geometria/tela/restauração) | konsoll + metronom + notes, unificados |
| **`Rask.Dialogs`** | `NativeDialogs` genérico: `alert`, `confirm`, `prompt(text)`, **`askSaveChanges()`** (Salvar / Não salvar / Cancelar); `AboutPanel` | konsoll |
| **`Rask.Application`** | `QuitGuard` (o app pergunta antes de encerrar), `DockMenu` configurável, `RecentDocuments` (`NSDocumentController` → menu "Abrir recente" nativo), `FileOpenHandler` (`QFileOpenEvent`, "Abrir com" do Finder) | konsoll + novo |
| **`Rask.Updater`** | `Updater` (Sparkle, canal beta) + `RaskSparkle.cmake` + `update-appcast.py` | konsoll |
| **`Rask.Controls`** | `TabStrip`, `TabButton`, `GlassSurface`, `ToolButton`, `SearchBar`, `Menu`, `MenuItem`, `FormGroup`, `FormRow`, `SidebarItem` | konsoll (generalizados) |
| **`Rask.Style`** | Estilo Qt Quick Controls para macOS (estende `QtQuick.Controls.macOS`) | metronom `MacStyle` |
| `cmake/RaskApp.cmake` | `rask_version_from_git()`, `rask_bundle_version()`, `rask_macos_bundle()` (Info.plist, assinatura ad hoc, prune, ícones) | konsoll + metronom |

Os módulos são criados **sob demanda**, na etapa do Tekst que precisa deles
(ver seção 3), e não todos de uma vez.

### Migração dos apps existentes

1. **metronom** é o primeiro a migrar (`Rask.Window`, `Rask.Platform`,
   `Rask.Style`, `Rask.Updater`): é pequeno e valida o funcionamento em Android/Linux.
2. **konsoll** migra depois, módulo a módulo, sem bloquear o Tekst.
3. **notes** troca `Rask.Controls.Native` pelos novos módulos.

---

## 3. Etapas do Tekst

### Etapa 0 — Fundação

- `git init`, `.gitignore`, `.clang-format`, `.clang-tidy` (copiar do konsoll),
  `AGENTS.md`, `README.md`, `LICENSE`.
- `CMakeLists.txt`: alvo `tekst`, módulo QML `Tekst`, `MACOSX_BUNDLE`,
  `Info.plist.in`, versão a partir da tag git, assinatura ad hoc pós-build.
- `main.cpp`: `QGuiApplication`, organização/domínio para o `QSettings`,
  `QQmlApplicationEngine` carregando `Tekst.Main`.
- Ícone: `assets/tekst.icon` (Icon Composer, paleta **Natt**), gerado por
  `tools/make-app-icons.py`; compilar com `actool` como o
  `cmake/AppIcons.cmake` do konsoll (Assets.car + .icns de fallback).
- Workflow do GitHub Actions compilando no macOS.

**Pronto quando:** `cmake --build` gera `Tekst.app` e ele abre uma janela vazia.

---

### Etapa 1 — Editor de texto simples (MVP)

Uma janela, um documento. Somente Qt, ainda sem código nativo.

**C++**
- `Document` (`QML_ELEMENT`): `fileUrl`, `fileName`, `modified`, `text`
  ligado ao `QQuickTextDocument` do `TextArea`; `load(url)`, `save()`,
  `saveAs(url)`. UTF-8, preserva o final de linha original (LF/CRLF) e a
  newline final, escrita atômica com `QSaveFile`, sinal `error(message)`.

**QML**
- `Main.qml`: `ApplicationWindow` com `ScrollView` + `TextArea`, fonte
  monoespaçada do sistema (`QFontDatabase::FixedFont`), sem quebra de linha
  configurável ainda (padrão: quebra ligada).
- `MenuBar` (vira a barra de menus nativa no macOS): **Arquivo** (Novo,
  Abrir…, Salvar, Salvar como…, Fechar), **Editar** (Desfazer, Refazer, Recortar,
  Copiar, Colar, Selecionar tudo).
- Atalhos padrão (`StandardKey`): ⌘N, ⌘O, ⌘S, ⇧⌘S, ⌘W, ⌘Z, ⇧⌘Z, ⌘X/C/V, ⌘A.
- `FileDialog` (`QtQuick.Dialogs`, que já usa o painel nativo no macOS).
- Título: `nome.txt — Tekst`, "Sem título" para documento novo.
- Ao fechar com alterações não salvas: `MessageDialog` Salvar / Não salvar / Cancelar.
- Abrir arquivo passado na linha de comando (`tekst arquivo.txt`).

**Testes**
- QtTest para `Document`: round-trip UTF-8, CRLF preservado, arquivo
  inexistente, `modified` após editar/salvar.

**Pronto quando:** dá para criar, abrir, editar, desfazer, salvar e fechar um
arquivo sem perder dados, sem nenhum código nativo.

> Limite conhecido: o `TextArea` do Qt Quick fica lento em arquivos grandes
> (vários MB). Isso é aceito no MVP e tratado na Etapa 7.

---

### Etapa 2 — Criar `rask-qml` e dar a cara de app Mac

Primeira etapa que gera código compartilhado.

- Criar o repositório `rask-qml` com `cmake/RaskApp.cmake`, **`Rask.Platform`**
  e **`Rask.Window`** (unificando `WindowTranslucency` do konsoll,
  `NativeWindow` do metronom e `MacOSWindow` do notes).
- Tekst: adicionar como submódulo; janela com conteúdo sob a title bar
  (`Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint`), fundo
  translúcido com blur (opcional), cores de texto/seleção/cursor de
  `SystemColors`, segue claro/escuro.
- **`Rask.Dialogs`**: substituir o `MessageDialog` pelo `NSAlert` nativo de
  "Salvar alterações?" (com sheet anexado à janela).
- Migrar o **metronom** para `Rask.Window` + `Rask.Platform` (prova de que a
  API serve a dois apps e funciona fora do macOS).

**Pronto quando:** o Tekst tem translucidez/blur e diálogos nativos, e o
metronom compila nas três plataformas usando `rask-qml`.

---

### Etapa 3 — Integração com o sistema de documentos do macOS

- Várias janelas, uma por documento (⌘N abre nova janela); controlador de
  janelas em `Main.qml`.
- `NativeWindow.representedFile` → ícone de proxy na title bar e menu de
  caminho com ⌘+clique; `documentEdited` → ponto no botão de fechar.
- **`Rask.Application`**:
  - `FileOpenHandler`: "Abrir com" do Finder e arrastar para o ícone do Dock
    (`QFileOpenEvent`); tipos de documento (`public.plain-text`,
    `public.source-code`, …) no `Info.plist`.
  - `RecentDocuments`: menu "Abrir recente" nativo.
  - `QuitGuard`: ⌘Q revisa os documentos não salvos de todas as janelas.
- Arrastar arquivo para dentro da janela abre o arquivo.
- Detectar alteração externa do arquivo (`QFileSystemWatcher`) → recarregar
  automaticamente se não houver edições, perguntar se houver.
- Restaurar janelas/documentos abertos ao reabrir (`WindowState`).

---

### Etapa 4 — Abas

- **`Rask.Controls`**: extrair `TabStrip`/`TabButton` genéricos do konsoll
  (modelo de abas, arrastar para reordenar, botão fechar, indicador de
  alteração, respeitando o `leadingInset` dos semáforos).
- Tekst: vários documentos por janela; ⌘T nova aba, ⌘⇧[ / ⌘⇧] navegar,
  ⌘1…9; mover aba para nova janela / entre janelas.
- Migrar o konsoll para o `TabStrip` comum (pode ser feito em paralelo).

---

### Etapa 5 — Recursos de editor

- Buscar/substituir (`SearchBar` de `Rask.Controls`): ⌘F, ⌘G/⇧⌘G, ⌥⌘F,
  regex, diferenciar maiúsculas, destacar ocorrências.
- Ir para linha (⌘L).
- Gutter com números de linha e destaque da linha atual.
- Quebra de linha liga/desliga; mostrar caracteres invisíveis.
- Indentação: tabs/espaços, largura, auto-indent, ⌘[ / ⌘] indentar a seleção.
- Barra de status: linha:coluna, seleção, codificação, final de linha,
  indentação, linguagem.
- Zoom da fonte (⌘+ / ⌘- / ⌘0).
- Codificações além de UTF-8 (detecção + "Reabrir com codificação").
- Janela de Ajustes (⌘,) com `FormGroup`/`FormRow`; `QSettings`.

---

### Etapa 6 — Realce de sintaxe

- **KSyntaxHighlighting** (KDE Frameworks, MIT, mais de 300 linguagens,
  temas) ligado ao `QQuickTextDocument` via `SyntaxHighlighter`.
- Detecção da linguagem pela extensão/shebang, seletor na barra de status.
- Temas claro/escuro acompanhando `Appearance`.
- Avaliar o impacto no desempenho antes de seguir para a Etapa 7.

---

### Etapa 7 — Desempenho e arquivos grandes

Critério de entrada: medir abertura, digitação e rolagem com arquivos de
1 MB, 10 MB e 100 MB. Se o `TextArea` não atender:

- Modelo próprio de texto (piece table ou rope) em C++, com undo/redo próprio.
- `EditorView` (`QQuickItem`) renderizando só as linhas visíveis — o mesmo
  princípio do `TerminalRenderer` do konsoll — com cursor, seleção, IME
  (`inputMethodQuery`) e acessibilidade.
- Realce incremental reaproveitando os estados do KSyntaxHighlighting.

É a maior etapa; por isso fica depois de o app já ser útil.

---

### Etapa 8 — Polimento e distribuição

- **`Rask.Updater`** (Sparkle) + appcast + release via GitHub Actions
  (assinatura Developer ID e notarização, como em `konsoll/docs/RELEASE.md`).
- Painel Sobre (`Rask.Dialogs.AboutPanel`), ícone do app.
- Liquid Glass (`GlassEffect`) na tab strip/barra de status no macOS 26.
- **`Rask.Style`** extraído do metronom e adotado pelo Tekst.
- Site (GitHub Pages) seguindo o padrão do konsoll/metronom.

---

## 4. Itens futuros (fora do escopo inicial)

Minimap, múltiplos cursores, split view, Markdown com preview, LSP
(autocompletar/diagnósticos), modo de comparação (diff), integração com
Quick Look e Spotlight, suporte a AppleScript/Atalhos.

## 5. Decisões em aberto

- Nome do repositório comum: `rask-qml` (proposto) vs. `rask-kit` / `rask`.
- Submódulo git vs. `FetchContent` com tag.
- Se o `Rask.Style` vira um estilo próprio selecionável via
  `QQuickStyle::setStyle("Rask.Style")` ou continua como extensão do estilo `macOS`.
