# hdrcal

Assistente de calibração **visual** de HDR para Hyprland, equivalente ao
*Windows HDR Calibration*: mede os três extremos do tone mapping (luminância
mínima, máxima e máxima de frame completo) **sem colorímetro**, persiste o
resultado e o reaplica a cada ativação de HDR. Calibrar uma vez, usar sempre.

## Escopo

Entra no escopo:

- os três testes visuais de calibração;
- persistência do perfil em `~/.config/hypr/hdrcal.lua`;
- integração com Hyprland (`hyprctl`), incluindo pré-condições de HDR e regra
  temporária de tone mapping;
- conversões ST 2084 (PQ) usadas pelos padrões e pela CLI.

Não entra no escopo: calibração de cor, gamut, white point, ICC, LUT 3D,
plugin de compositor e suporte a outros compositores.

## Status dos milestones

| Milestone | Estado | Observação |
|---|---|---|
| M0 — spike do pipeline HDR | **concluído em 2026-10-19** | mpv/libplacebo entregou superfície PQ 10-bit; KMS confirmou `eotf=2` e `max bpc=10`; comparação visual 653/1023 × 712/1023 confirmada pelo usuário. |
| M1 — núcleo | **concluído em 2026-10-19** | `pq`, `profile`, `hypr`, `kms`, `preflight`, parser JSON, CLI e testes unitários. |
| M2 — wizard com backend mpv | pendente | padrões, três fases, IPC do mpv, gravação e reload. |
| M3 — app nativo Vulkan/Wayland | pendente | swapchain 10-bit, `VK_COLOR_SPACE_HDR10_ST2084_EXT`, `wp-color-management-v1`. |
| M4 — qualidade | pendente | snapshot/comparação DDC, export JSON, toggle baseado em perfil. |
| M5 — release interno | pendente | README final e teste A/B de reprodutibilidade. |

Detalhes e critérios de aceite: [`docs/DEVELOPMENT_PLAN.md`](docs/DEVELOPMENT_PLAN.md).
Decisões técnicas e resultado do M0: [`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md).

## Compilar e testar

Dependências de build (M1):

- compilador C++20 (`clang++` ou `g++`);
- CMake ≥ 3.20 e `ctest`;
- `pkg-config` e headers de desenvolvimento do `libdrm`;
- `hyprctl` para os comandos que falam com a sessão viva.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

O binário fica em `build/hdrcal`. A build usa
`-Wall -Wextra -Wpedantic -Werror`; todo o código é formatado com
`clang-format` e o `.clang-format` do repositório.

Resultado real da última execução: ver a seção "Última verificação" no
[`CHANGELOG.md`](CHANGELOG.md).

## Uso (M1)

Gravar um perfil:

```sh
hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350
```

O comando valida o perfil, grava `~/.config/hypr/hdrcal.lua` de forma atômica
(tmp + `fsync` + `rename`) e executa `hyprctl reload`, salvo com
`--no-reload`. O snapshot opcional do OSD pode ser incluído:

```sh
hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350 \
  --osd-brightness 80 --osd-contrast 55 --osd-color-preset User
```

Pré-condições de HDR (seção 8.2 das instruções):

```sh
hdrcal preflight --output DP-2
```

Cada uma das cinco verificações é impressa com `[PASS]`/`[FAIL]`; o código de
saída é `0` somente se todas passarem. `--config-dir` permite apontar outro
diretório de configuração do Hyprland.

Diagnóstico somente-leitura do KMS e do monitor:

```sh
hdrcal probe --output DP-2
```

## Integração com o Hyprland

O arquivo gerado é uma **tabela Lua** com os campos que o Hyprland consome:

```lua
return {
    version = 1,
    profiles = {
        ["DP-2"] = {
            min_luminance = 0.02,
            max_luminance = 600,
            max_avg_luminance = 350,
            calibrated_at = "2026-10-19T21:00:00Z",
        },
    },
}
```

Para que o perfil seja aplicado, a configuração do monitor precisa consumir a
tabela. O projeto **não altera** `monitors.lua` automaticamente: a integração
é um ponto acordado com o usuário (seção 12 das instruções). Exemplo de
consumo em `monitors.lua`:

```lua
local hdrcal = require("hdrcal")
local profile = hdrcal.profiles["DP-2"]

hl.monitor({
    output = "DP-2",
    mode = "2560x1440@120",
    position = "0x0",
    scale = "1",
    bitdepth = 10,
    cm = "hdr",
    min_luminance = profile and profile.min_luminance or nil,
    max_luminance = profile and profile.max_luminance or nil,
    max_avg_luminance = profile and profile.max_avg_luminance or nil,
})
```

O `require("hdrcal")` é relido em cada `hyprctl reload`; o cache de módulos Lua
do Hyprland é limpo antes da releitura.

## Limitações honestas (M1)

- A ferramenta calibra a **cadeia compositor + monitor**, não o painel nu. O
  tone mapping e o ABL do firmware não são removíveis por software; o
  resultado é uma calibração visual operacional, não uma garantia de precisão.
- O arquivo `hdrcal.lua` contém **um** perfil por vez. Multi-monitor ainda não
  é suportado: gravar um novo perfil substitui o arquivo inteiro.
- O ICC ativo não é consultável em runtime no Hyprland 0.56.2. O preflight usa
  uma varredura conservadora dos arquivos `.lua`/`.conf` do diretório de
  configuração: qualquer atribuição `icc` não vazia faz a verificação falhar.
  Comentários Lua e `icc` vazio são ignorados.
- A regra temporária de luminância é aplicada com `hyprctl eval` (runtime) e
  restaurada com `hyprctl reload`. A restauração em SIGINT/SIGTERM e watchdog
  será ligada ao wizard interativo no M2; nesta entrega existe o guard RAII
  para saídas normais.
- O M1 não desenha padrões nem conduz o usuário pelos três testes; isso é M2.
- `hyprctl keyword` não funciona com a configuração Lua do Hyprland 0.56.2;
  toda alteração de runtime usa `hyprctl eval`.

## Estrutura do repositório

```text
include/hdrcal/   cabeçalhos públicos (pq, profile, hypr, kms, preflight, json)
src/              implementação do núcleo e CLI
tests/            testes unitários sem GPU/compositor
docs/             padrões de código, plano e decisões técnicas
scripts/          gerador de padrão PQ do spike M0 (sem dependências externas)
```

## Documentos obrigatórios

- [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md)
- [`docs/DEVELOPMENT_PLAN.md`](docs/DEVELOPMENT_PLAN.md)
- [`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md)
- [`CHANGELOG.md`](CHANGELOG.md)
