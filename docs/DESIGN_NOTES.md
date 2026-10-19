# Decisões técnicas e alternativas rejeitadas

Este documento registra decisões que não são óbvias pelo código, com a
evidência que as sustenta. Toda decisão nova ou correção de erro entra aqui e no
`CHANGELOG.md`.

## M0 — resultado do spike do pipeline HDR (2026-10-19)

**Ambiente:** Hyprland 0.56.2, mpv 0.41.0 com libplacebo 7.360.1, FFmpeg 9.0.1,
GPU RADV, monitor TCL 27G64 em `DP-2` (2560×1440@120, `bitdepth=10`,
`cm="hdr"`, `XRGB2101010`).

**Procedimento executado:**

1. Padrão PNG `rgb48be` 2560×1440 com metade esquerda em 653/1023 e metade
   direita em 712/1023, com chunk `cICP` BT.2020/PQ/full-range.
2. Regra temporária de tone mapping aplicada em runtime com
   `hyprctl eval 'hl.monitor({ output = "DP-2", min_luminance = 0.0, max_luminance = 10000, max_avg_luminance = 10000 })'`.
3. `render:cm_auto_hdr` colocado em `0` em runtime com
   `hyprctl eval 'hl.config({ render = { cm_auto_hdr = 0 } })'`.
4. mpv em fullscreen:
   `--target-colorspace-hint=yes --vo=gpu-next --tone-mapping=clip --hdr-compute-peak=no`.
5. Leitura KMS read-only via libdrm durante a execução.
6. Restauração com `hyprctl reload`; `render:cm_auto_hdr` voltou a `1`.

**Evidência objetiva:**

- mpv escolheu `VK_FORMAT_A2B10G10R10_UNORM_PACK32 + VK_COLOR_SPACE_HDR10_ST2084_EXT`.
- log do mpv: `Compositor supports setting mastering display primaries.`,
  `transfer: pq, primaries: bt.2020`.
- KMS durante a execução:
  `max bpc=10`, `HDR_OUTPUT_METADATA.eotf=2`, `max_cll=603`,
  `max_fall=603`.
- Cliente mpv em fullscreen no monitor 0, 2560×1440.

**Evidência visual:** o usuário confirmou que a metade direita (712/1023,
~600 nits) ficou **bem mais clara** que a esquerda (653/1023, ~350 nits).

**Conclusão:** pipeline aprovado. M0 não bloqueia o projeto.

**Ressalva registrada:** o blob KMS continuou reportando `max_cll=603` (fallback
de EDID) mesmo com a regra temporária de `max_luminance=10000`. Isso é esperado
para o estado de desktop, mas significa que o efeito da regra **não** foi
verificado por leitura do infoframe. A verificação de que o compositor deixa de
tonemapar conteúdo acima do pico do painel fica para o M2, com um padrão acima
de 603 nits. No M0, 600 < 603, então a conclusão visual não depende disso.

## Descobertas de ambiente

### `hyprctl keyword` não funciona com config Lua

No Hyprland 0.56.2 deste sistema, a configuração é Lua. A tentativa
`hyprctl keyword render:cm_auto_hdr 0` retorna:

```text
keyword can't work with non-legacy parsers. Use eval.
```

**Decisão:** toda alteração de runtime usa `hyprctl eval`. Para opções de
configuração, `hl.config({ ... })`; para regras de monitor, `hl.monitor({ ... })`.
`hyprctl eval` continua sendo runtime apenas — nunca persistência.

**Alternativa rejeitada:** usar a sintaxe legada `monitor=...`. O parser legado
aceita `mirror`, `bitdepth`, `cm`, `sdrbrightness`, `sdrsaturation`,
`transform`, `vrr` e `icc`, mas **não** aceita `min_luminance`,
`max_luminance` ou `max_avg_luminance`. Além disso, o sistema está em config Lua.

### O encoder PNG do FFmpeg 9.0.1 não grava metadados de cor

Com `-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc`, o PNG
gerado não contém `cICP`, `iCCP` nem `sRGB`; `ffprobe` mostra
`color_transfer=unknown` e `color_primaries=unknown`. O encoder `png` também não
expõe opção de metadados de cor.

**Decisão para o M0:** gerar os pixels e o chunk `cICP` (4 bytes: primárias 9 =
BT.2020, transferência 16 = PQ, matriz 0 = RGB, full range 1) diretamente no
script `scripts/generate_pq_pattern.py`. Verificado: após a geração,
`ffprobe` reporta `color_transfer=smpte2084`, `color_primaries=bt2020`, e o mpv
trata a imagem como PQ.

**Impacto no M2:** o plano original dizia “PNG 16-bit via ffmpeg com metadados”.
O FFmpeg não faz isso sozinho. O M2 deve continuar gerando os pixels via FFmpeg
e pós-processar o `cICP` no próprio hdrcal, ou usar outro contêiner (por exemplo
FFV1/MKV) com as tags corretas. A decisão final fica para o M2; a alternativa
“confiar nas flags de cor do FFmpeg para PNG” foi rejeitada por evidência.

### Consulta de ICC em runtime não existe no Hyprland 0.56.2

Tentativas de leitura:

- `hyprctl getoption monitorv2:DP-2:icc` → `no such option`;
- `hyprctl monitors -j` não expõe ICC;
- `hl.get_config("monitorv2:DP-2:icc")` → chave desconhecida;
- `hl.get_monitor("DP-2")` expõe `cm`, mas não o arquivo ICC.

**Decisão:** o preflight faz uma varredura conservadora dos arquivos `.lua` e
`.conf` do diretório de configuração do Hyprland. Qualquer atribuição `icc` de
monitor com valor não vazio reprova a verificação. Comentários Lua (`--` e
blocos `--[[ ]]`) e `icc` vazio são ignorados. Arquivo de configuração ilegível
também reprova, em vez de ser aceito silenciosamente.

**Limitação assumida:** a varredura é textual, não é um interpretador Lua.
Configurações geradas dinamicamente que não contenham a palavra `icc` no texto
podem escapar. O preflight é conservador, não onisciente. A alternativa
“ignorar ICC” foi rejeitada porque viola a pré-condição 4 da seção 8.2.

**Alternativa rejeitada:** plugin de compositor ou patch no Hyprland para expor
o ICC. Está fora do escopo rígido do projeto.

### Leitura KMS sem root

`/dev/dri/card*` tem grupo `video`; o usuário está nesse grupo. A leitura de
`drmModeGetResources`, `drmModeGetConnector`, propriedades e blob
`HDR_OUTPUT_METADATA` funciona com `O_RDONLY`, sem DRM master. O hdrcal usa
libdrm e não abre o dispositivo para escrita.

**Alternativa rejeitada:** `modetest`/`drm_info` externos. Não estão instalados e
introduziriam dependência de ferramenta e parsing de texto. libdrm dá acesso
tipado ao blob.

### JSON de `hyprctl monitors`

**Decisão:** parser JSON próprio, pequeno e testado, em `src/json.cpp`. O
projeto precisa apenas de objetos, arrays, strings, números, booleanos e null.
**Alternativa rejeitada:** adicionar nlohmann/json ou outra dependência para
ler um único endpoint. O parser tem testes de escapes, surrogate pairs, erros
de sintaxe e chaves duplicadas.

## Modelo e persistência do perfil

**Decisão:** `CalibrationProfile` com `minimumLuminanceNits` (double, unidade
nits), `maximumLuminanceNits` (int) e `maximumFullFrameLuminanceNits` (int),
porque é assim que o Hyprland declara os campos (`min_luminance` float,
`max_luminance` e `max_avg_luminance` ints). O timestamp é UTC ISO 8601 com
segundos.

**Formato `hdrcal.lua`:** tabela Lua com `version = 1` e `profiles` indexado pelo
nome da saída. O `require("hdrcal")` devolve a tabela; o consumidor (config do
monitor) decide quais campos aplicar. O projeto não reescreve `monitors.lua`.

**Números:** `std::to_chars` para doubles, com `.` decimal e representação
mínima que faz round-trip. `std::to_string` para inteiros. Nenhuma dependência
de locale do processo. Teste verifica que `0.02` não vira `0,02`.

**Escrita atômica:** arquivo temporário no mesmo diretório (`hdrcal.lua.tmp.<pid>`),
`write` em loop, `fsync`, `close`, `rename`. Em erro, o temporário é removido.
O destino contém o perfil antigo completo ou o novo completo, nunca um arquivo
truncado. `fsync` do diretório não foi incluído; a durabilidade do rename após
queda de energia não faz parte do aceite do M1.

**Limitação:** o arquivo tem um perfil. Um segundo monitor sobrescreveria o
primeiro. Multi-monitor não está no escopo do M1 e exigirá merge de tabela
(provavelmente com leitura do arquivo existente) no M2/M4.

## Regra temporária e restauração

**Decisão:** `TemporaryLuminanceRule` aplica
`hl.monitor({ output = ..., min_luminance = 0, max_luminance = 10000, max_avg_luminance = 10000 })`
via `hyprctl eval` e restaura com `hyprctl reload`. O guard é RAII: o destrutor
restaura em saída normal e em exceção. O wizard M2 chamará `restore()`
explicitamente antes de SIGINT/SIGTERM e no watchdog.

**Por que `reload`:** ele relê a configuração do monitor (incluindo
`monitors.lua` e `hdrcal.lua`) e reaplica a regra persistida. Reaplicar valores
capturados de `hyprctl monitors -j` não restaura `min_luminance`,
`max_luminance` e `max_avg_luminance`, porque esses campos não aparecem no JSON.

**Alternativa rejeitada:** `hyprctl keyword monitor ...`. Não aceita os campos de
luminância e não funciona com config Lua.

## Pré-condições (seção 8.2)

O preflight executa as cinco verificações, sempre reportando todas:

1. `HDR_OUTPUT_METADATA.eotf == 2` via KMS.
2. `max bpc == 10` via KMS **e** `currentFormat` contendo `2101010` via
   `hyprctl monitors -j`.
3. `render:cm_auto_hdr == 0`.
4. nenhum ICC ativo (varredura conservadora, ver acima).
5. `render:direct_scanout == 0`.

A escolha de ler KMS em vez de confiar só no Hyprland é deliberada: o estado do
compositor pode dizer `cm=hdr` enquanto o KMS ainda não recebeu metadata PQ.

## Erros e idioma

Mensagens de erro de biblioteca e CLI são pt-BR porque são exibidas ao usuário.
Identificadores, nomes de teste e comentários de código são em inglês. A CLI
retorna `2` para erro de uso e `1` para falha operacional; `preflight` retorna
`1` se qualquer verificação falhar.

## Fora de escopo (rejeitado por princípio)

- calibração de cor/gamut/white point;
- ICC/LUT 3D como parte do produto;
- plugin de compositor;
- suporte a compositores que não o Hyprland;
- “precisão garantida”: o texto do projeto fala em calibração visual
  operacional da cadeia compositor + monitor.
