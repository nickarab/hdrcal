# Changelog

Todas as mudanças, decisões e correções do hdrcal. Mudança sem entrada aqui não
existe. Datas em UTC.

## 2026-10-19

### Criado

- Repositório `~/hdrcal` com C++20, CMake e `ctest`.
- Documentos obrigatórios: `README.md`, `docs/CODING_STANDARDS.md`,
  `docs/DEVELOPMENT_PLAN.md`, `docs/DESIGN_NOTES.md` e este `CHANGELOG.md`.
- `.clang-format` e `.gitignore`.
- Script `scripts/generate_pq_pattern.py`: PNG 16-bit com metade esquerda e
  direita em códigos PQ distintos e chunk `cICP` BT.2020/PQ.

### M0 — spike do pipeline HDR: concluído

- Padrão PNG 2560×1440 `rgb48be`, 653/1023 (~350 nits) à esquerda e 712/1023
  (~600 nits) à direita, com `cICP` BT.2020/PQ/full-range.
- Regra temporária aplicada em runtime:
  `hyprctl eval 'hl.monitor({ output = "DP-2", min_luminance = 0.0, max_luminance = 10000, max_avg_luminance = 10000 })'`.
- `render:cm_auto_hdr` em `0` durante o teste, restaurado depois.
- mpv 0.41/libplacebo 7.360 em fullscreen escolheu
  `VK_FORMAT_A2B10G10R10_UNORM_PACK32 + VK_COLOR_SPACE_HDR10_ST2084_EXT`.
- KMS durante a execução: `max bpc=10`, `eotf=2`, `max_cll=603`,
  `max_fall=603`.
- Usuário confirmou visualmente que a metade direita (712/1023) ficou bem mais
  clara que a esquerda (653/1023).
- Sessão restaurada com `hyprctl reload`; `render:cm_auto_hdr` voltou a `1`.

### M1 — núcleo: concluído

- `pq`: EOTF/OETF ST 2084, conversões de amostra 10-bit, clamp de bordas,
  roundtrip e tabela permanente de âncoras.
- `profile`: `CalibrationProfile` com saída, mínimo (double), máximo (int),
  máximo full-frame (int), timestamp UTC ISO 8601 e snapshot opcional do OSD;
  validação; renderização Lua locale-independent; escrita atômica com
  `tmp` + `fsync` + `rename`.
- `json`: parser mínimo com objetos, arrays, strings, escapes, surrogate pairs,
  números, booleanos e null; erros com posição.
- `command`: execução direta (sem shell) com captura de stdout/stderr e código
  de saída.
- `hypr`: wrapper injetável de `hyprctl` (`getoption`, `monitors -j`, `reload`,
  `eval`), regra temporária de luminância e guard RAII com restauração.
- `kms`: leitura read-only de `max bpc` e `HDR_OUTPUT_METADATA` via libdrm,
  com decodificação testável do blob.
- `preflight`: as cinco pré-condições da seção 8.2, com injeção de KMS e
  comandos nos testes.
- CLI: `profile write`, `preflight`, `probe`.

### Decidido

- `hyprctl keyword` não funciona com a configuração Lua do Hyprland 0.56.2;
  toda alteração de runtime usa `hyprctl eval`.
- A regra temporária de luminância é aplicada via `hl.monitor({...})` e
  restaurada via `hyprctl reload`; o JSON de `monitors` não expõe os campos de
  luminância para restauração direta.
- Se `restore()` falha, o guard mantém o estado aplicado e tenta a restauração
  novamente no destrutor; o erro continua sendo devolvido ao chamador.
- A persistência é uma tabela Lua em `~/.config/hypr/hdrcal.lua`. O projeto não
  altera `monitors.lua` sem pedido explícito; a integração de consumo está
  documentada no README.
- O ICC ativo é verificado por varredura conservadora dos arquivos `.lua` e
  `.conf`, porque o Hyprland 0.56.2 não expõe consulta de ICC em runtime.
- Parser JSON próprio em vez de dependência externa para um único endpoint.
- O formato `hdrcal.lua` contém um perfil por arquivo no M1; multi-monitor fica
  para M2/M4.

### Corrigido

- Primeira tentativa de M0 usou `hyprctl keyword render:cm_auto_hdr 0`, que é
  rejeitado pelo parser Lua do Hyprland. Corrigido para `hl.config({...})` via
  `hyprctl eval`.
- Primeira tentativa de M0 usou `--gpu-api=vulkan --gpu-context=wayland` no
  mpv; o contexto explícito não inicializou. O contexto automático
  (`waylandvk`) funciona e foi usado.
- FFmpeg 9.0.1 não grava metadados de cor em PNG; o script passou a escrever o
  chunk `cICP` diretamente.

### Última verificação

```text
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build -j
$ ctest --test-dir build --output-on-failure
Test project /home/manoel/hdrcal/build
    Start 1: hdrcal_unit_tests
1/1 Test #1: hdrcal_unit_tests ................   Passed    0.00 sec

100% tests passed out of 1

$ ./build/hdrcal_tests
61 passaram, 0 falharam
```

Também compilado sem warnings com `clang++` em `build-clang` (mesmos 61 testes
verdes).

```text
$ ./build/hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350 \
    --path /tmp/opencode/hdrcal-acceptance/hdrcal.lua --no-reload
exit=0
# arquivo contém min_luminance = 0.02, max_luminance = 600,
# max_avg_luminance = 350 e calibrated_at em ISO 8601 UTC.

$ ./build/hdrcal probe --output DP-2
KMS DP-2:
  max bpc: 10
  eotf: 2
  max_cll: 603
  max_fall: 603
Hyprland DP-2:
  currentFormat: XRGB2101010
  colorManagementPreset: hdr
```

```text
$ ./build/hdrcal preflight --output DP-2   # com cm_auto_hdr temporariamente 0
[PASS] HDR ativo no KMS (eotf = 2) — eotf=2 (PQ) no conector DP-2
[PASS] Saída em 10 bits (max bpc e framebuffer) — max bpc=10, framebuffer=XRGB2101010
[PASS] Troca automática de HDR desligada — render:cm_auto_hdr=0, esperado 0
[PASS] Nenhum ICC ativo no monitor — nenhum 'icc' de monitor encontrado em /home/manoel/.config/hypr
[PASS] Direct scanout desligado — render:direct_scanout=0, esperado 0
preflight_exit=0
```

### Pendente

- M2: wizard com mpv, padrões 16-bit e restauração em SIGINT/SIGTERM/watchdog.
- M2: verificar com padrão acima de 603 nits que a regra temporária
  `max_luminance=10000` impede o tone mapping do compositor.
- M3, M4 e M5 conforme `docs/DEVELOPMENT_PLAN.md`.
