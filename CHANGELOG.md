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

### M2 — wizard com backend mpv: implementado, smoke-testado e aceito

- **Padrões:** gerador C++/zlib de PNG RGB 16-bit com `cICP` BT.2020/PQ,
  resolução lida do monitor. Fase mínima com mancha central, máxima com xadrez
  `código`/`código-16` e frame completo uniforme. Escrita atômica.
- **Wizard:** máquina de estados pura (`CalibrationWizard`) com três fases,
  códigos iniciais 100/700/592, passo 1 e passo grosso 10, `Enter` salva e
  `Esc` aborta. Testes cobrem avanço, clamp, roundtrip de luminância e abort.
- **mpv:** `MpvIpcClient` JSON sobre Unix socket, `MpvProcess` RAII, script Lua
  embutido com `mp.add_forced_key_binding` encaminhando teclas por FIFO.
  `loadfile` troca o padrão; `show-text` mostra código/nits e instruções.
- **Restauração:** guard da regra temporária + watchdog pós-crash por
  `socketpair`/double-fork. O watchdog mata o mpv órfão e roda
  `hyprctl reload` se o processo principal desaparecer sem `done`. SIGINT/
  SIGTERM são tratados por flag `sig_atomic_t` e re-levantados após a
  restauração. Testes cobrem crash, graceful stop e timeout de FIFO.
- **CLI:** `hdrcal wizard --output <nome> [--profile-path <arquivo>]
  [--config-dir <diretório>]`.
- **Corrigido:** `SessionDirectory` era copiável apesar do destrutor; a cópia
  temporária removia o diretório da sessão antes do `mkfifo`. Passou a ser
  move-only.
- **Corrigido:** `mp.add_key_binding` não recebia as teclas sintéticas de
  `hl.dsp.send_shortcut`; os bindings passaram a ser
  `mp.add_forced_key_binding`.

**Smoke test ao vivo:** três fases concluídas com teclas sintéticas, perfil
gravado em caminho temporário e reload ok; `Esc` abortou sem gravar; SIGTERM
restaurou a sessão sem mpv órfão; watchdog passou nos testes de crash e
graceful. `ctest`: 117 testes verdes.

**Aceite do M2:** o usuário aceitou o wizard em 2026-10-19 e autorizou
expressamente a integração no `monitors.lua`. Os valores do smoke test não são
uma calibração real; o aceite formal não os promove a perfil de produção.

**Pendente do M2:** a integração explícita em `monitors.lua` (W4). O projeto não
altera essa configuração sem pedido. A documentação do wizard passou a incluir o
pré-requisito manual `render:cm_auto_hdr=0`; o `hyprctl reload` final devolve o
valor ao padrão.

### Correções pós-auditoria independente (2026-10-19)

#### P1 — regra temporária segura fora do setup auditado

- **Corrigido:** o guard da regra temporária não confiava mais no merge
  implícito do `hl.monitor`. Antes de aplicar, verifica via `hyprctl monitors -j`
  e varredura do diretório de configuração que existe uma regra com `output`
  exatamente igual à saída (Lua `hl.monitor`, inclusive multi-linha, ou
  `monitor=` legado). Ausente a regra, falha com erro pt-BR que explica o
  requisito de merge do Hyprland 0.56.2 e como ajustar `monitors.lua`; o hdrcal
  não edita a config do usuário.
- **Adicionado:** pós-verificação obrigatória após o `eval`. O guard compara
  `eotf`, `max bpc` e `currentFormat` antes/depois; se houve degradação (ou se
  a releitura falhou), executa `hyprctl reload`, devolve erro e não mantém a
  regra aplicada. Falha do próprio `reload` mantém `m_applied` para o destrutor
  tentar novamente.
- **Testes:** `FakeHyprCommands` cobre regra existente, regra ausente
  (`desc:`), degradação detectada na pós-verificação com restauração disparada,
  falha de releitura pós-aplicação e retry de restauração.
- **Documentado:** semântica de merge do `hl.monitor` no `DESIGN_NOTES` com
  citação do fonte (`LuaBindingsConfigRules.cpp`, `hlMonitor`, v0.56.2) e
  pré-requisito da regra por nome no README.

#### P2 — portabilidade, versões e multi-GPU

- **Documentado:** nova seção "Portabilidade" no README com pacotes de build
  por família (Ubuntu/Debian e Arch/Noctalia), requisitos de runtime, piso de
  versões e a declaração explícita de que NVIDIA não foi testada.
- **Adicionado:** `preflight` lê `hyprctl version`, exige Hyprland ≥ 0.55 e
  avisa (sem reprovar) quando a versão é maior que a auditada 0.56.2. A
  verificação virou o sexto item reportado.
- **Adicionado:** CMake exige `libdrm >= 2.4.99`, GCC ≥ 11 ou Clang ≥ 14, com
  erro de configure em pt-BR.
- **Corrigido:** seleção de card KMS extraída para
  `selectKmsCardForConnector`. Dois cards com o mesmo nome de conector agora
  produzem erro de ambiguidade com todos os paths, em vez de escolher o
  primeiro silenciosamente. Testes unitários injetam a lista de cards.
- **Adicionado:** variável `HDRICAL_PROFILE_PATH` como override do caminho do
  perfil, com prioridade `--path`/`--profile-path` > env > XDG. Documentado no
  `--help`, README e `DESIGN_NOTES`, com teste da resolução.

#### P3 — conformidade de nomes e histórico

- `requireValue` renomeado para `findMissingValueError` (a função devolve a
  mensagem de erro, não um valor). Comportamento inalterado.
- Constantes ST 2084 renomeadas e documentadas por papel:
  `kPqM1LuminanceExponent`, `kPqM2SignalExponent`,
  `kPqC1RationalOffset`, `kPqC2RationalNumeratorScale`,
  `kPqC3RationalDenominatorScale`.
- A normalização de zero negativo em `formatLocaleIndependentDouble` ganhou
  comentário do porquê e teste que prova a saída canônica `0` (em vez de `-0`).
- A seção "Última verificação" passou a declarar o estado da sessão em que cada
  captura foi feita.

### Correções de UX do wizard (pós-aceite do M2)

#### W2 — instrução da fase não some mais durante o ajuste

- **Corrigido:** a instrução da fase era enviada no mesmo `show-text` do valor
  atual, com duração de 4000 ms. O OSD embutido do mpv esconde o texto sozinho
  depois da duração, então a instrução desaparecia no meio da fase. A instrução
  passou a usar um overlay ASS permanente (`osd-overlay`, rodapé, sem timeout),
  atualizado apenas quando a fase muda; o código/nits continuam em `show-text`
  efêmero, em outra camada, sem apagar nem cobrir a instrução.
- O `MpvIpcClient` ganhou `showPermanentInstruction` e o construtor testável
  `buildMpvInstructionOverlayJson`, que usa argumentos nomeados (exigidos pelo
  `osd-overlay`) e converte quebras de linha em `\N` do ASS.

#### W3 — segurar as setas repete e acelera

- **Corrigido:** os bindings gerados em `key-forward.lua` usavam
  `mp.add_forced_key_binding` sem `repeatable`, então o mpv disparava uma única
  vez por pressão física. As setas (finas e `Shift`) passaram a ser
  `repeatable`; `Enter`/`Esc` permanecem single-shot.
- O mpv usa autorepeat próprio com `--native-keyrepeat=no`,
  `--input-ar-delay=200` e `--input-ar-rate=10` (em vez dos 40/s padrão). A taxa
  menor evita encher o FIFO enquanto cada passo regenera o padrão (~54 ms
  medidos para 2560×1440) e troca o arquivo pelo IPC.
- `CalibrationWizard::handleKeyAt` recebe o instante da tecla. Repetições da
  mesma seta com intervalo de até 300 ms são uma pressão contínua; após 1000 ms
  o passo vira o passo grosso existente (10). Pausa, troca de direção ou reset
  de relógio reiniciam no passo 1.
- `Enter` e `Escape` têm debounce de 250 ms e a janela é estendida enquanto a
  rajada continua, então segurar `Enter` avança no máximo uma fase. Testes de
  unidade novos: passo 1 → 10 no limiar, pausa, troca de direção, reset de
  relógio e rajada de `Enter`.
- `handleKey` continua existindo para chamadas sem relógio; ele espaça as
  pressões acima do debounce, preservando os testes e usos antigos.

#### W4 — integração autorizada no `monitors.lua`

- **Integração feita** em `~/.config/hypr/monitors.lua` com autorização
  expressa do usuário. O restante do arquivo (output, modo, posição, scale,
  `bitdepth = 10`, `cm = "hdr"`) não mudou. Diff aplicado:

  ```lua
  +local ok, hdrcal = pcall(require, "hdrcal")
  +local profile = ok and hdrcal and hdrcal.profiles and hdrcal.profiles["DP-2"] or nil
  +
   hl.monitor({
       output = "DP-2",
       ...
       bitdepth = 10,
       cm = "hdr",
  +    min_luminance = profile and profile.min_luminance or nil,
  +    max_luminance = profile and profile.max_luminance or nil,
  +    max_avg_luminance = profile and profile.max_avg_luminance or nil,
   })
  ```

- **Fallback sem perfil:** `pcall` impede que a ausência de `hdrcal.lua`
  derrube a config. Verificado ao vivo: reload sem config errors, tabela
  capturada sem os campos de luminância, `probe` em `max bpc=10`, `eotf=2`,
  `XRGB2101010`.
- **Com perfil:** perfil temporário `0.02/600/350` foi lido pelo
  `require("hdrcal")` e os três campos chegaram ao `hl.monitor` (capturados com
  wrapper em `hyprctl eval`); `probe` continuou em 10 bits/HDR. O perfil
  temporário foi removido e o reload final voltou ao fallback sem perfil.
- **Documentado:** a regra permanente "o hdrcal não edita config do usuário"
  continua valendo; esta foi a única exceção autorizada, registrada em
  `docs/DESIGN_NOTES.md`.
- O exemplo do README foi alinhado ao código real (incluindo o `pcall`).

#### W5 — comando explícito `hdrcal integrate`

- **Adicionado:** `hdrcal integrate --output <saída> [--config-dir <dir>]
  [--apply]`. Sem `--apply` é dry-run: imprime diff unificado e não escreve;
  `--apply` é o único caminho que edita a config.
- **Seleção:** `findMonitorRuleMatches` devolve todas as regras com `output`
  exatamente igual à saída. Zero regras, múltiplas regras (em qualquer arquivo)
  ou regra legada `monitor=` viram erro pt-BR acionável; legado é recusado
  porque `require("hdrcal")` só existe na config Lua.
- **Editor puro:** `planMonitorRuleIntegration` insere o bloco
  `pcall(require, "hdrcal")` + `hdrcalProfile` e os três campos
  (`min_luminance`, `max_luminance`, `max_avg_luminance`) antes do `}`,
  preservando todos os campos existentes. Ignora comentários/strings, suporta
  tabela multi-linha e de uma linha e é idempotente: com os três campos
  presentes, o diff é vazio e nada é escrito. Campos parciais são recusados
  para não duplicar chaves.
- **Transação:** `ConfigEditTransaction` grava
  `<arquivo>.bak.hdrcal.<timestamp>`, substitui com `tmp + fsync + rename`,
  preserva as permissões e restaura o original se a validação falhar ou se o
  objeto for destruído antes do commit. A validação executa `hyprctl reload`,
  confere `hyprctl configerrors` por erros que mencionem o arquivo editado,
  relê a regra (`findMonitorRuleLocation`) e replaneja o texto para confirmar os
  três campos.
- **CLI:** `hdrcal integrate --help`/`-h` mostra ajuda específica com opções,
  dry-run, backup e exemplos; o `--help` global documenta o dry-run. README
  passa a recomendar o comando e mantém o exemplo manual como alternativa;
  DESIGN_NOTES registra a decisão "plug explícito ≠ automagicidade".
- **Testes:** 14 novos (planner, idempotência, campos preservados, fallback sem
  perfil, ambiguidade, legado, tabela de uma linha, comentários, backup,
  rollback e RAII). Total: **136 passaram, 0 falharam**.

**Verificação ao vivo (2026-10-19):**

```text
$ ./build/hdrcal integrate --output DP-2
já integrado: /home/manoel/.config/hypr/monitors.lua:4 já contém min_luminance, max_luminance e max_avg_luminance; nenhuma alteração necessária.
exit=0
```

Config sintética em `/tmp/opencode/hdrcal-integrate-test`:

```text
$ ./build/hdrcal integrate --output DP-2 --config-dir /tmp/opencode/hdrcal-integrate-test --apply
--- a//tmp/opencode/hdrcal-integrate-test/monitors.lua
+++ b//tmp/opencode/hdrcal-integrate-test/monitors.lua
@@ -1,3 +1,6 @@
+local hdrcalOk, hdrcalModule = pcall(require, "hdrcal")
+local hdrcalProfile = hdrcalOk and hdrcalModule and hdrcalModule.profiles and hdrcalModule.profiles["DP-2"] or nil
+
 hl.monitor({
...
backup: /tmp/opencode/hdrcal-integrate-test/monitors.lua.bak.hdrcal.2026-10-19T23-05-37Z
integração aplicada em /tmp/opencode/hdrcal-integrate-test/monitors.lua
exit=0

$ ./build/hdrcal integrate --output DP-2 --config-dir /tmp/opencode/hdrcal-integrate-test --apply
já integrado: ... nenhuma alteração necessária.
# nenhum segundo backup foi criado
```

Rollback forçado com um `hyprctl` falso que falha no `reload`:

```text
$ PATH=/tmp/opencode/hdrcal-fakebin:$PATH ./build/hdrcal integrate --output DP-2 \
    --config-dir /tmp/opencode/hdrcal-int-rollback --apply
backup: /tmp/opencode/hdrcal-int-rollback/monitors.lua.bak.hdrcal.2026-10-19T23-07-10Z
erro: validação falhou: hyprctl reload falhou (código 1); alteração desfeita a partir do backup ...
exit=1

$ diff -u original.expected monitors.lua
# sem diferenças: conteúdo original restaurado; backup preservado
```

Arquivo somente-leitura: `--apply` recusa antes de escrever com
`erro: sem permissão de escrita em ...: Permission denied` (exit 1) e o
conteúdo permanece idêntico.

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
- M2: o gerador de padrões é interno (C++/zlib + `cICP`), não uma invocação de
  FFmpeg por passo. Evidência: o FFmpeg 9.0.1 não grava `cICP` em PNG, leva
  100–300 ms por padrão e a cadeia de escala introduziu 1–3 LSB de erro; o
  gerador interno é exato e rápido.
- M2: teclas são encaminhadas por um script Lua embutido + FIFO, com
  `mp.add_forced_key_binding`. O binding normal não recebeu as teclas
  sintéticas de `hl.dsp.send_shortcut`; o forçado recebeu.
- M2: o watchdog é um processo destacado por `socketpair` + double-fork. Sem
  `done`, ele mata o mpv órfão e roda `hyprctl reload`.
- M2: as fases usam códigos iniciais 100/700/592, passo 1 e passo grosso 10; a
  máxima usa xadrez `código`/`código-16` para expor clipping.

### Corrigido

- Primeira tentativa de M0 usou `hyprctl keyword render:cm_auto_hdr 0`, que é
  rejeitado pelo parser Lua do Hyprland. Corrigido para `hl.config({...})` via
  `hyprctl eval`.
- Primeira tentativa de M0 usou `--gpu-api=vulkan --gpu-context=wayland` no
  mpv; o contexto explícito não inicializou. O contexto automático
  (`waylandvk`) funciona e foi usado.
- FFmpeg 9.0.1 não grava metadados de cor em PNG; o script passou a escrever o
  chunk `cICP` diretamente.

### Última verificação (pós-correções P1–P3)

**Legenda do estado da sessão em cada captura:**

- Build e `ctest`: não tocam na sessão viva.
- `probe` e `preflight`: capturados com a sessão viva já em HDR
  (`bitdepth=10`, `cm="hdr"`, KMS `eotf=2`, `max bpc=10`), **sem** regra
  temporária aplicada. O valor `render:cm_auto_hdr=1` no `preflight` é o padrão
  do usuário e não é um estado permanente criado pelo hdrcal; a captura
  histórica com `cm_auto_hdr=0` exigia alteração temporária de runtime.
- `profile write`: executado com `--no-reload` e com
  `HDRICAL_PROFILE_PATH` apontando para `/tmp`; não alterou a sessão nem a
  config do usuário.

Ambiente: GCC 16.2.0, Clang 23.1.0, libdrm 2.4.134, Hyprland 0.56.2.

```text
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
-- Checking for module 'libdrm>=2.4.99'
--   Found libdrm, version 2.4.134
-- Configuring done
-- Generating done

$ cmake --build build -j
[100%] Built target hdrcal

$ ctest --test-dir build --output-on-failure
Test project /home/manoel/hdrcal/build
    Start 1: hdrcal_unit_tests
1/1 Test #1: hdrcal_unit_tests ................   Passed    0.76 sec

100% tests passed out of 1

$ ./build/hdrcal_tests
117 passaram, 0 falharam
```

Build com Clang, também com `-Werror` e sem warnings:

```text
$ cmake -S . -B build-clang -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
$ cmake --build build-clang -j
[100%] Built target hdrcal

$ ctest --test-dir build-clang --output-on-failure
Test project /home/manoel/hdrcal/build-clang
    Start 1: hdrcal_unit_tests
1/1 Test #1: hdrcal_unit_tests ................   Passed    0.76 sec

100% tests passed out of 1
```

```text
$ clang-format --dry-run --Werror $(find include src tests -name '*.cpp' -o -name '*.hpp')
exit=0  # zero violações
```

```text
$ HDRICAL_PROFILE_PATH=/tmp/opencode/hdrcal-acceptance/env-profile.lua \
    ./build/hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350 --no-reload
Perfil gravado em /tmp/opencode/hdrcal-acceptance/env-profile.lua
  saída: DP-2
  min: 0.02 nits
  max: 600 nits
  max full-frame: 350 nits
write_exit=0
# arquivo contém min_luminance = 0.02, max_luminance = 600,
# max_avg_luminance = 350 e calibrated_at em ISO 8601 UTC.
```

```text
$ ./build/hdrcal probe --output DP-2
KMS DP-2:
  max bpc: 10
  eotf: 2
  max_cll: 603
  max_fall: 603
Hyprland DP-2:
  currentFormat: XRGB2101010
  colorManagementPreset: hdr
probe_exit=0
```

```text
$ ./build/hdrcal preflight --output DP-2   # sessão em HDR, cm_auto_hdr=1 (padrão)
[PASS] HDR ativo no KMS (eotf = 2) — eotf=2 (PQ) no conector DP-2
[PASS] Saída em 10 bits (max bpc e framebuffer) — max bpc=10, framebuffer=XRGB2101010
[FAIL] Troca automática de HDR desligada — render:cm_auto_hdr=1, esperado 0
[PASS] Nenhum ICC ativo no monitor — nenhum 'icc' de monitor encontrado em /home/manoel/.config/hypr
[PASS] Direct scanout desligado — render:direct_scanout=0, esperado 0
[PASS] Versão do Hyprland compatível (>= 0.55) — Hyprland 0.56.2 (auditada: 0.56.2)
preflight_exit=1
```

O `[FAIL]` acima é o comportamento correto e esperado: a captura foi feita sem
desligar temporariamente `render:cm_auto_hdr`. O hdrcal não alterou essa opção.

#### M2 — smoke test do wizard (teclas sintéticas)

O wizard foi executado ao vivo com `render:cm_auto_hdr=0` temporário, mpv em
fullscreen na `DP-2` e teclas enviadas por `hl.dsp.send_shortcut` (encaminhadas
pelo script Lua). Resultado:

```text
Pré-condições OK. Abrindo mpv em fullscreen (2560x1440)...
Fase 1/3 ...
Fase 2/3 ...
Fase 3/3 ...
Calibração concluída. Perfil salvo em /tmp/opencode/hdrcal-m2-final/hdrcal.lua
  min_luminance = 0.35650839475326696
  max_luminance = 537
  max_avg_luminance = 181
Para jogos: min_luminance=0.3565... max_luminance=537 max_avg_luminance=181
```

O perfil foi gravado, o reload rodou e `cm_auto_hdr` voltou a `1`; nenhum mpv
ficou órfão. Os valores **não** são uma calibração real: as teclas sintéticas
produziram eventos duplicados/perdidos em alguns passos. O teste prova a
mecânica, não o resultado visual.

Abort com `Esc`: o wizard saiu com código 1, não criou perfil, removeu o mpv e
o diretório temporário, e restaurou `cm_auto_hdr=1`. SIGTERM no meio da sessão:
mesma restauração, sem mpv órfão. Watchdog: testes de crash (mata o mpv e roda
o comando de restauração) e de encerramento gracioso passaram.

### Última verificação (W1–W4, 2026-10-19)

Ambiente: Hyprland 0.56.2, DP-2 em HDR (`eotf=2`, `max bpc=10`,
`XRGB2101010`), perfil `~/.config/hypr/hdrcal.lua` ausente no teste de fallback.

```text
$ cmake --build build --clean-first -j
[100%] Built target hdrcal
[100%] Built target hdrcal_tests
# GCC, -Wall -Wextra -Wpedantic -Werror: zero warnings

$ cmake --build build-clang --clean-first -j
[100%] Built target hdrcal_tests
[100%] Built target hdrcal
# Clang, mesmas flags: zero warnings

$ ctest --test-dir build --output-on-failure
1/1 Test #1: hdrcal_unit_tests ................   Passed    0.76 sec
100% tests passed out of 1
Total Test time (real) =   0.76 sec

$ ./build/hdrcal_tests | tail -1
122 passaram, 0 falharam

$ clang-format --dry-run --Werror $(find include src tests -name '*.cpp' -o -name '*.hpp')
exit=0  # zero violações
```

`monitors.lua` integrado, fallback sem `hdrcal.lua` (tabela capturada com
wrapper em `hyprctl eval`, sem erros de config):

```text
$ hyprctl configerrors
(vazio)

$ # tabela passada ao hl.monitor
bitdepth=10
cm=hdr
mode=2560x1440@120
output=DP-2
position=0x0
scale=1

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

Com perfil temporário (`0.02/600/350`) o `require("hdrcal")` foi lido e os três
campos chegaram ao `hl.monitor`; o `probe` continuou em 10 bits/HDR. O perfil
temporário foi removido e o reload final voltou ao fallback.

Wizard ao vivo com as correções W2/W3 (teclas sintéticas, não é calibração
real):

```text
$ hyprctl eval 'hl.config({ render = { cm_auto_hdr = 0 } })'
$ ./build/hdrcal wizard --output DP-2 --profile-path /tmp/opencode/hdrcal-w2w4/hdrcal.lua
Pré-condições OK. Abrindo mpv em fullscreen (2560x1440)...
Fase 1/3 — luminância mínima: ...
Fase 2/3 — luminância máxima: ...
Fase 3/3 — frame completo: ...

Calibração concluída. Perfil salvo em /tmp/opencode/hdrcal-w2w4/hdrcal.lua
  min_luminance = 0.002642409835434442
  max_luminance = 691
  max_avg_luminance = 604
exit=0
```

O `osd-overlay` permanente foi aceito (o wizard abortaria se o IPC retornasse
erro). As teclas sintéticas sofreram eventos perdidos/atrasados; os valores
acima são mecânica, não calibração. Após o wizard: `render:cm_auto_hdr=1`, sem
mpv órfão, diretório de sessão removido e `probe` ainda em 10 bits/HDR.

### Pendente

- M2: verificar com padrão acima de 603 nits que a regra temporária
  `max_luminance=10000` impede o tone mapping do compositor.
- M3, M4 e M5 conforme `docs/DEVELOPMENT_PLAN.md`.
