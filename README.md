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
| M2 — wizard com backend mpv | **aceito pelo usuário em 2026-10-19** | padrões 16-bit/PQ, IPC do mpv, três fases, regra temporária com guard + watchdog, perfil e reload. A integração no `monitors.lua` e o consumo do perfil estão documentados abaixo. |
| M3 — app nativo Vulkan/Wayland | pendente | swapchain 10-bit, `VK_COLOR_SPACE_HDR10_ST2084_EXT`, `wp-color-management-v1`. |
| M4 — qualidade | pendente | snapshot/comparação DDC, export JSON, toggle baseado em perfil. |
| M5 — release interno | pendente | README final e teste A/B de reprodutibilidade. |

Detalhes e critérios de aceite: [`docs/DEVELOPMENT_PLAN.md`](docs/DEVELOPMENT_PLAN.md).
Decisões técnicas e resultados do M0/M2: [`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md).
Correções de portabilidade e segurança da regra temporária (P1–P3, pós-auditoria
de 2026-10-19): [`CHANGELOG.md`](CHANGELOG.md).

## Compilar e testar

Dependências de build (M1/M2):

- compilador C++20: **GCC ≥ 11** ou **Clang ≥ 14** (designated
  initializers, ranges e `charconv`);
- CMake ≥ 3.20 e `ctest`;
- `pkg-config` e **libdrm ≥ 2.4.99** (headers de desenvolvimento; o código usa
  `drmModeGetConnectorCurrent`);
- `hyprctl` para os comandos que falam com a sessão viva;
- `mpv` ≥ 0.40 em runtime para o wizard visual (não é dependência de build).

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

## Portabilidade

Alvo: **Noctalia + Hyprland em qualquer distribuição Linux**. O fallback
suportado e testado são distros baseadas em Ubuntu (a PikaOS usada no
desenvolvimento é Ubuntu-based). O código não contém caminho, saída ou GPU
específicos da máquina de desenvolvimento.

Pacotes de **build** por família:

- Ubuntu/Debian-based:
  `sudo apt install build-essential cmake pkg-config libdrm-dev mpv`;
- Arch/Noctalia:
  `sudo pacman -S --needed base-devel cmake pkgconf libdrm mpv`.

O `mpv` só é necessário para o wizard visual (M2); os comandos `probe` e
`preflight` funcionam sem ele.

Requisitos de **runtime**:

- sessão Hyprland **≥ 0.55** com configuração Lua e `hyprctl eval` (testado em
  0.56.2; versões mais novas geram aviso, não bloqueio);
- `hyprctl` no `PATH`;
- usuário com acesso de leitura a `/dev/dri/card*` (grupos `video` e/ou
  `render`; nenhuma escrita ou DRM master é necessária);
- GPU com driver Mesa/RADV. O M0 foi validado em RADV; **NVIDIA não foi
  testado** e não há código específico para ela.

Versões mínimas são checadas ou documentadas:

| Componente | Mínimo | Onde é verificado |
|---|---|---|
| Hyprland | 0.55 | `hdrcal preflight` executa `hyprctl version` e reprova abaixo do piso; avisa acima de 0.56.2 |
| libdrm | 2.4.99 | `pkg_check_modules(LIBDRM ... libdrm>=2.4.99)` no configure do CMake |
| GCC / Clang | 11 / 14 | checagem de configure do CMake, além desta documentação |

## Uso

Calibração visual completa (M2):

```sh
hdrcal wizard --output DP-2
```

O wizard verifica as pré-condições, aplica a regra temporária de tone mapping,
abre o mpv em fullscreen na saída indicada e conduz três fases:

> **Pré-requisito:** a pré-condição 3 exige `render:cm_auto_hdr = 0`. Ajuste a
> sessão antes de iniciar (o `hyprctl reload` final do wizard devolve o valor
> ao padrão da configuração, normalmente `1`):
>
> ```sh
> hyprctl eval 'hl.config({ render = { cm_auto_hdr = 0 } })'
> ```

1. **luminância mínima** — mancha escura sobre preto; ajuste até ficar apenas
   visível;
2. **luminância máxima** — mancha clara com xadrez; aumente até o xadrez
   desaparecer e volte um passo;
3. **frame completo** — campo branco de tela cheia; ajuste até o brilho máximo
   estável.

Durante o wizard: `←`/`→` mudam 1 código e, com a tecla pressionada, repetem
continuamente (após ~1 s de pressão contínua o passo sobe para 10, o mesmo
passo grosso de `Shift+←`/`Shift+→`). `Enter` salva a fase e `Esc` aborta, sem
repetição: segurar essas teclas não salva nem aborta em loop. A instrução da
fase fica fixa no rodapé do mpv durante toda a fase; o código e a luminância
calculada aparecem em um OSD separado e efêmero a cada ajuste. Ao final, o
perfil é gravado e o resultado é impresso no terminal, incluindo uma linha
pronta para colar em jogos.

Opções: `--profile-path <arquivo>` (prioridade máxima), `--config-dir
<diretório>` e `HDRICAL_PROFILE_PATH`. O padrão é
`$XDG_CONFIG_HOME/hypr/hdrcal.lua`.

Gravar um perfil manualmente (M1):

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

O caminho do perfil segue a prioridade **`--path` / `--profile-path` >
`HDRICAL_PROFILE_PATH` > `$XDG_CONFIG_HOME/hypr/hdrcal.lua`**. A variável de
ambiente existe porque o `require("hdrcal")` da config Lua resolve a partir do
diretório de configuração, que pode não ser `~/.config/hypr` em instalações
fora do padrão.

Pré-condições de HDR (seção 8.2 das instruções) e checagem de versão do
Hyprland:

```sh
hdrcal preflight --output DP-2
```

As cinco verificações de HDR e a verificação de versão são impressas com
`[PASS]`/`[FAIL]`; o código de saída é `0` somente se todas passarem.
`--config-dir` permite apontar outro diretório de configuração do Hyprland.

**Pré-requisito da regra temporária:** a configuração do usuário precisa ter
uma regra de monitor cujo `output` seja **exatamente** o nome da saída (por
exemplo, `output = "DP-2"`). Regras chaveadas por `desc:...`, `make:...` ou
curingas não são suficientes: o merge parcial do `hl.monitor` no Hyprland
0.56.2 só copia a regra existente quando o nome coincide. Se a regra não
existir, o `wizard` e o guard da regra temporária falham **antes** de alterar a
sessão, com instruções para ajustar a config; o hdrcal nunca edita
`monitors.lua` por conta própria.

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
consumo em `monitors.lua`, seguro quando o perfil ainda não existe:

```lua
local ok, hdrcal = pcall(require, "hdrcal")
local profile = ok and hdrcal and hdrcal.profiles and hdrcal.profiles["DP-2"] or nil

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

O `pcall` mantém a config carregando quando `hdrcal.lua` não existe. Campos com
valor `nil` são omitidos da tabela, então o `hl.monitor` resultante preserva
`bitdepth`/`cm` e não inventa luminâncias. Com perfil, os três campos são
passados ao compositor. A integração foi aplicada ao `monitors.lua` deste
usuário em 2026-10-19 com autorização explícita (W4); a regra permanente do
projeto continua sendo não editar config de usuário por conta própria.

O `require("hdrcal")` é relido em cada `hyprctl reload`; o cache de módulos Lua
do Hyprland é limpo antes da releitura.

## Limitações honestas (M2)

- A ferramenta calibra a **cadeia compositor + monitor**, não o painel nu. O
  tone mapping e o ABL do firmware não são removíveis por software; o
  resultado é uma calibração visual operacional, não uma garantia de precisão.
- O arquivo `hdrcal.lua` contém **um** perfil por vez. Multi-monitor ainda não
  é suportado: gravar um novo perfil substitui o arquivo inteiro.
- A aplicação automática do perfil exige que a configuração do monitor consuma
  `require("hdrcal")` (snippet acima). O wizard grava o arquivo e executa
  `hyprctl reload`, mas **não edita** `monitors.lua` sem pedido explícito.
- O M2 foi aceito pelo usuário em 2026-10-19. O wizard foi validado com teclas
  sintéticas (três fases, perfil, `Esc`, SIGTERM e watchdog); o aceite formal
  não transforma os valores do smoke test em uma calibração real.
- Os padrões são gerados pelo próprio hdrcal (PNG 16-bit + `cICP` via zlib), não
  por uma invocação de ffmpeg por passo. O ffmpeg 9.0.1 não grava `cICP` em PNG
  e leva 100–300 ms por padrão; a decisão e as medições estão no
  `docs/DESIGN_NOTES.md`.
- O ICC ativo não é consultável em runtime no Hyprland 0.56.2. O preflight usa
  uma varredura conservadora dos arquivos `.lua`/`.conf` do diretório de
  configuração: qualquer atribuição `icc` não vazia faz a verificação falhar.
  Comentários Lua e `icc` vazio são ignorados.
- Em multi-GPU, se dois cards expõem o mesmo nome de conector (por exemplo,
  `DP-2` em ambos), `probe`/`preflight`/`wizard` falham com a lista dos cards em
  vez de escolher um silenciosamente.
- A regra temporária de luminância é aplicada com `hyprctl eval` (runtime) e
  restaurada com `hyprctl reload`. Antes de aplicar, o guard exige uma regra com
  o nome exato da saída; depois de aplicar, re-lê KMS e Hyprland e restaura
  imediatamente se `eotf`, `max bpc` ou o formato 10-bit degradarem. O wizard
  restaura em sucesso, cancelamento, SIGINT/SIGTERM e watchdog pós-crash.
- `hyprctl keyword` não funciona com a configuração Lua do Hyprland 0.56.2;
  toda alteração de runtime usa `hyprctl eval`.

## Estrutura do repositório

```text
include/hdrcal/   cabeçalhos públicos (pq, profile, hypr, hypr_config, kms,
                  preflight, json, wizard, wizard_session, mpv, pattern,
                  process, watchdog)
src/              implementação do núcleo, do wizard e da CLI
tests/            testes unitários sem GPU/compositor ou interação humana
docs/             padrões de código, plano e decisões técnicas
scripts/          gerador de padrão PQ do spike M0 (sem dependências externas)
```

## Documentos obrigatórios

- [`docs/CODING_STANDARDS.md`](docs/CODING_STANDARDS.md)
- [`docs/DEVELOPMENT_PLAN.md`](docs/DEVELOPMENT_PLAN.md)
- [`docs/DESIGN_NOTES.md`](docs/DESIGN_NOTES.md)
- [`CHANGELOG.md`](CHANGELOG.md)
