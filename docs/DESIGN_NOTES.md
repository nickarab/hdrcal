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

## M2 — wizard com backend mpv (2026-10-19)

**Arquitetura:** o wizard é uma máquina de estados pura (`CalibrationWizard`)
alimentada por teclas, com uma camada de sessão que integra Hyprland, mpv,
padrões, watchdog e persistência. Nenhum teste unitário exige GPU, compositor
ou interação.

**Padrões:** PNG RGB 16-bit, 2560×1440 (resolução lida do monitor), com `cICP`
BT.2020/PQ/full-range. Fases:

- mínima: mancha central de 25% da tela sobre preto;
- máxima: mancha central com xadrez de 16 px alternando `código` e
  `código - 16`, para tornar o clipping visível;
- frame completo: campo uniforme de tela cheia.

Códigos iniciais: 100 (mínima), 700 (máxima) e 592 (frame completo). Setas
mudam 1 código; Shift+setas mudam 10. O perfil registra a luminância calculada
por ST 2084, com máximo e full-frame arredondados para inteiros (campos int do
Hyprland).

**mpv:** processo filho em grupo próprio, fullscreen por
`--fs-screen-name=<saída>`, `--target-colorspace-hint=yes`, `--vo=gpu-next`,
`--tone-mapping=clip`, `--hdr-compute-peak=no`, `--no-input-default-bindings`,
`--no-osc`, `--image-display-duration=inf`, `--scale=nearest` e
`--input-ipc-server=<socket>`. O padrão é trocado por
`{"command":["loadfile",...]}`. A instrução da fase usa um overlay ASS
permanente (`osd-overlay`, rodapé, sem timeout); o código e a luminância usam
`show-text` efêmero. São camadas separadas: o `show-text` não apaga nem cobre a
instrução, e o overlay só muda quando a fase muda. O terminal recebe as
instruções e o resumo; o feedback visual fica no OSD do mpv.

**Teclas:** um script Lua embutido (`kMpvKeyForwardScript`) é escrito no
diretório da sessão e carregado com `--script`. Ele abre o FIFO
`$HDRCAL_KEY_FIFO` e escreve `left`, `right`, `shift-left`, `shift-right`,
`enter` ou `escape` por linha. O hdrcal lê o FIFO em modo não bloqueante e
mapeia para `WizardKey`. Usamos `mp.add_forced_key_binding`, não
`mp.add_key_binding`: em teste controlado, `hl.dsp.send_shortcut` entregou a
tecla ao mpv (`[input] No key binding found for key 'RIGHT'`) mas não acionou o
binding normal do script; o binding forçado acionou. O usuário aceitou o M2 em
2026-10-19.

**Repetição e aceleração:** os bindings de seta são `repeatable`; o mpv usa
autorepeat próprio (`--native-keyrepeat=no`, `--input-ar-delay=200`,
`--input-ar-rate=10`) em vez do repeat do sistema. A taxa é 10/s, não os 40/s
padrão: cada repetição regenera o PNG e recarrega pelo IPC (medido em ~54 ms
para 2560×1440), e 40/s encheria o FIFO e continuaria movendo depois de soltar
a tecla. O `CalibrationWizard` recebe o instante de cada tecla
(`handleKeyAt`), considera continuação apenas repetições com intervalo de até
300 ms e troca do passo 1 para o passo grosso 10 após 1000 ms de pressão
contínua. `Enter`/`Escape` não são `repeatable` e ainda têm debounce de 250 ms
no estado puro: uma rajada de `Enter` avança no máximo uma fase. Os testes de
unidade usam marcadores de tempo e cobrem passo 1 → 10, pausa, troca de
direção, reset de relógio e rajada de `Enter`.

**Restauração:** o guard RAII restaura em saída normal, exceção e `Esc`. O
wizard instala handlers de SIGINT/SIGTERM que apenas setam um
`volatile sig_atomic_t`; o loop polla o FIFO em fatias de 100 ms, vê a flag,
restaura e re-levanta o sinal com o handler default. O watchdog é um processo
destacado por double-fork, ligado ao hdrcal por `socketpair`. O pai envia
`mpv <pid>` após lançar o mpv e `done` no encerramento normal. Se o socket
fechar sem `done` (crash, SIGKILL, exceção não tratada), o watchdog mata o mpv
com SIGTERM, espera 200 ms e executa `hyprctl reload`. Testes cobrem o caminho
de crash e o encerramento gracioso, incluindo a morte do mpv órfão.

**Bug corrigido no caminho:** `SessionDirectory` tinha destrutor customizado e
cópia implícita; ao mover o `optional<SessionDirectory>` para a sessão, a cópia
temporária era destruída e apagava o diretório antes do `mkfifo`. O tipo passou
a ser move-only com `path.clear()` no moved-from; o M2 não teria funcionado sem
essa correção.

**Smoke test ao vivo (2026-10-19):**

- preflight com `cm_auto_hdr=0`: seis verificações `[PASS]`, incluindo a versão
  do Hyprland;
- três fases concluídas com teclas sintéticas encaminhadas pelo script Lua
  (`hl.dsp.send_shortcut`), perfil salvo em caminho temporário, reload ok;
- `Esc`: abortou sem gravar perfil, removeu o mpv e o diretório temporário,
  `cm_auto_hdr` voltou a `1`;
- SIGTERM no meio da sessão: restauração confirmada, nenhum mpv órfão,
  `cm_auto_hdr=1`;
- watchdog: testes de crash e graceful passaram;
- 117 testes unitários verdes.

Os valores do smoke test (min 0.3565 nits, max 537, avg 181) **não** são uma
calibração real: as teclas sintéticas geraram eventos duplicados/perdidos em
alguns passos. Eles só provam a mecânica. O usuário aceitou o M2 em 2026-10-19;
os valores do smoke test não foram promovidos a perfil de produção.

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

**Decisão para o M0 (histórica):** gerar os pixels e o chunk `cICP` (4 bytes:
primárias 9 = BT.2020, transferência 16 = PQ, matriz 0 = RGB, full range 1)
diretamente em um script Python de uso único, removido depois que o gerador C++
do M2 o substituiu. Verificado: após a geração,
`ffprobe` reporta `color_transfer=smpte2084`, `color_primaries=bt2020`, e o mpv
trata a imagem como PQ.

**Decisão final do M2:** os padrões são gerados pelo próprio hdrcal em C++ com
zlib e um chunk `cICP` explícito. Medições no ambiente de referência:

- FFmpeg 9.0.1 leva ~0,10 s para um padrão 640×360 e ~0,30 s para 2560×1440,
  por invocação, além do custo de decodificação no mpv;
- o encoder PNG não grava `cICP`/`iCCP`/`sRGB`;
- a cadeia `crop`/`scale`/`pad` do FFmpeg introduziu 1–3 LSB de erro em amostras
  de 16 bits, o que é pequeno mas desnecessário para uma calibração.

O gerador interno produz os 16 bits exatos, escreve atomicamente e leva poucos
milissegundos. O FFmpeg continua sendo a referência do M0 e o formato foi
validado com `ffprobe` (PQ/BT.2020). A alternativa “uma invocação de FFmpeg por
passo” foi rejeitada por latência e pela impossibilidade de marcar o PNG
diretamente.

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
(provavelmente com leitura do arquivo existente) no M4.

## Regra temporária e restauração

**Decisão:** `TemporaryLuminanceRule` aplica
`hl.monitor({ output = ..., min_luminance = 0, max_luminance = 10000, max_avg_luminance = 10000 })`
via `hyprctl eval` e restaura com `hyprctl reload`. O guard é RAII: o destrutor
restaura em saída normal e em exceção. Se uma restauração explícita falhar, o
estado é mantido como aplicado e o destrutor tenta novamente. O wizard chama `restore()` explicitamente antes de SIGINT/SIGTERM e no watchdog.

**Por que `reload`:** ele relê a configuração do monitor (incluindo
`monitors.lua` e `hdrcal.lua`) e reaplica a regra persistida. Reaplicar valores
capturados de `hyprctl monitors -j` não restaura `min_luminance`,
`max_luminance` e `max_avg_luminance`, porque esses campos não aparecem no JSON.

**Alternativa rejeitada:** `hyprctl keyword monitor ...`. Não aceita os campos de
luminância e não funciona com config Lua.

### Semântica de merge do `hl.monitor` (Hyprland 0.56.2)

**Fato verificado no fonte** (`src/config/lua/bindings/LuaBindingsConfigRules.cpp`,
função `hlMonitor`):

```cpp
const auto existing = std::ranges::find_if(Config::monitorRuleMgr()->all(),
                                           [&output](const auto& rule) { return rule.m_name == output; });
if (existing != Config::monitorRuleMgr()->all().end())
    parser.rule() = *existing;
```

Ou seja, a chamada parcial `hl.monitor({ output = "DP-2", min_luminance = ... })`
**só faz merge** quando já existe uma regra cujo `m_name` é literalmente
`"DP-2"`. A comparação é de igualdade de string, não de seletor estático. Uma
regra por `desc:...`, `make:...` ou curinga (`""`) continua valendo para escolher
o monitor (`CMonitorRuleManager::get` usa `matchesStaticSelector`), mas **não é
copiada** no merge. Nesse caso o parser cria uma regra nova com os defaults de
`CMonitorRule`: `m_enable10bit = false` e `m_cmType = CM_SRGB`, isto é, 8 bits e
SDR.

**Consequência:** aplicar a regra temporária em uma config chaveada por
`desc:`/`make:` derrubaria o HDR para SDR no meio da medição, sem erro do
Hyprland. O hdrcal trata isso como pré-requisito, não como detalhe interno.

**Decisão (P1):**

1. **Pré-checagem antes de qualquer alteração:** o guard lê o diretório de
   configuração (mesma varredura textual usada pelo ICC) e exige uma regra com
   `output` igual ao nome da saída, em Lua (`hl.monitor`, inclusive multi-linha)
   ou no formato legado `monitor=DP-2,...` em `.conf`. Também consulta
   `hyprctl monitors -j` para confirmar que a saída existe e está ativa. Se a
   regra não existir, devolve erro pt-BR explicando o requisito e como ajustar
   `monitors.lua`; o hdrcal **não** altera a config do usuário.
2. **Pós-verificação obrigatória:** imediatamente antes do `eval`, o guard
   captura `HDR_OUTPUT_METADATA.eotf`, `max bpc` (KMS) e `currentFormat`
   (`hyprctl monitors -j`). Depois do `eval`, repete as duas leituras. Se
   `max bpc` diminuiu, o `eotf` deixou de ser 2 ou o formato perdeu o sufixo
   `2101010`, a regra é considerada degradante; o guard executa
   `hyprctl reload`, devolve erro e deixa `m_applied = false` em caso de
   restauração bem-sucedida. Se o próprio `reload` falhar, `m_applied` continua
   verdadeiro para o destrutor tentar de novo — a garantia de restauração tem
   precedência sobre o estado nominal, e o erro informa a nova tentativa.
3. **Falha de leitura também restaura:** não conseguir reler KMS/Hyprland após
   o `eval` é tratado como degradação, porque não há como garantir que a sessão
   está em HDR.

**Alternativa rejeitada:** confiar apenas no `reload` final. Ele restaura a
config persistida, mas a sessão intermediária já teria medido lixo e o usuário
teria visto a tela cair para SDR. **Alternativa rejeitada:** tentar “consertar”
a config do usuário automaticamente (por exemplo, inserir uma regra por nome).
Isso viola a seção 12 das instruções e muda o significado da configuração dele.

## Portabilidade e versões mínimas (P2)

O alvo passou a ser **Noctalia + Hyprland em qualquer distro**, com fallback
suportado e testado em distros baseadas em Ubuntu (PikaOS no desenvolvimento).
As escolhas:

- **Hyprland ≥ 0.55**: piso para config Lua e `hyprctl eval`. O `preflight` lê
  `hyprctl version`, interpreta `major.minor.patch` e reprova abaixo do piso.
  Acima de **0.56.2** (única versão auditada) a verificação passa com aviso
  explícito no detalhe, porque mudanças de comportamento do compositor não
  podem ser presumidas.
- **libdrm ≥ 2.4.99**: piso de `drmModeGetConnectorCurrent`, checado no
  `pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm>=2.4.99)` do CMake.
- **GCC ≥ 11 / Clang ≥ 14**: piso de C++20 com designated initializers, ranges
  e `charconv`, checado no configure do CMake e documentado no README.
- **NVIDIA**: não testado; o código não contém nada específico de RADV, mas a
  evidência do M0 é só RADV. O README declara a limitação em vez de prometer
  paridade.

### Multi-GPU sem escolha silenciosa

`readKmsConnectorState` antes percorria `/dev/dri/card*` e aceitava o primeiro
conector conectado com o nome pedido. Em um sistema com dois cards e o mesmo
`DP-2` nos dois, isso escolhia um card arbitrário. A seleção foi extraída para
`selectKmsCardForConnector`, que recebe a lista de cards e seus conectores
conectados: zero matches é erro de “conector não encontrado”, mais de um match é
erro de ambiguidade com **todos** os paths candidatos. A leitura de propriedades
só acontece no card escolhido. O teste unitário injeta a lista e não exige GPU.

### Caminho do perfil

O caminho passou a seguir `--path`/`--profile-path` > `HDRICAL_PROFILE_PATH` >
`$XDG_CONFIG_HOME/hypr/hdrcal.lua` (`$HOME/.config/hypr` como fallback). O
motivo é concreto: o `require("hdrcal")` da config Lua resolve a partir do
diretório de configuração do Hyprland, que pode ser diferente de
`~/.config/hypr` em instalações fora do padrão ou em configs versionadas. A
prioridade está no `--help`, no README e aqui, e a resolução pura tem teste
unitário (`resolveProfilePath`).

### Integração no `monitors.lua` (W4)

**Decisão:** a config do usuário consome o perfil com `pcall(require,
"hdrcal")`, lê `profiles["DP-2"]` e passa `min_luminance`, `max_luminance` e
`max_avg_luminance` ao `hl.monitor`. `pcall` foi escolhido porque `require` de
um módulo inexistente gera erro Lua; sem o `pcall`, a config inteira deixaria de
carregar antes da primeira calibração. Os campos usam o idioma
`profile and profile.campo or nil`; valores `nil` são omitidos da tabela e o
merge do `hl.monitor` preserva `bitdepth = 10`/`cm = "hdr"`.

**Verificação ao vivo (2026-10-19):** sem `hdrcal.lua`, `hyprctl reload` não
produziu config errors, a tabela capturada tinha apenas os campos originais e
o `probe` continuou em `max bpc=10`, `eotf=2`, `XRGB2101010`. Com um perfil
temporário (`0.02/600/350`), a tabela passou a incluir os três campos de
luminância e o `probe` continuou em 10 bits/HDR. O perfil temporário foi
removido depois; os valores do smoke test não foram promovidos a calibração.

**Regra permanente:** o hdrcal **não edita config do usuário**. A edição de
`~/.config/hypr/monitors.lua` em 2026-10-19 foi a única exceção, expressamente
autorizada pelo usuário para o W4. Integrações futuras exigem novo pedido.

### Plug explícito com `hdrcal integrate` (W5)

**Decisão:** o plug que a W4 fez manualmente vira um comando explícito, não um
passo automático de instalação/build/wizard. `hdrcal integrate` só toca a config
quando o usuário roda o comando; sem `--apply` ele é dry-run e imprime o diff.
Isso preserva a regra de não editar config alheia silenciosamente e resolve o
problema de outras máquinas precisarem copiar o exemplo do README.

**Seleção da regra:** o comando usa `findMonitorRuleMatches`, que devolve
**todas** as regras com `output` exatamente igual à saída. Zero regras vira erro
acionável; mais de uma (inclusive em arquivos diferentes) vira erro de
ambiguidade com a lista `arquivo:linha` e o formato (Lua/legado). Regra legada
`monitor=` é detectada, mas a edição é recusada: `require` só existe na config
Lua, então o erro pede conversão ou integração manual.

**Editor puro:** `planMonitorRuleIntegration(texto, saída)` localiza a única
chamada `hl.monitor` no texto cru, ignora comentários/strings, verifica os três
campos no nível superior da tabela e insere o bloco
`local hdrcalOk, hdrcalModule = pcall(require, "hdrcal")` +
`local hdrcalProfile = ...` antes da chamada, mais os três campos antes do `}`.
Campos existentes são preservados; se os três já estão presentes, o plano é
`alreadyIntegrated` e o diff fica vazio (idempotência). Campos parciais viram
erro para não duplicar chaves. Tabelas multi-linha e de uma linha são
suportadas; a chamada precisa começar em linha própria quando o bloco de perfil
será inserido, senão o comando recusa em vez de quebrar a expressão.

**Transação:** `ConfigEditTransaction` captura as permissões originais, grava
`<arquivo>.bak.hdrcal.<timestamp>` ao lado, substitui o arquivo com
`writeFileAtomically` (tmp + fsync + rename) e restaura o original se a
validação falhar ou se o objeto for destruído antes do commit. A validação roda
`hyprctl reload`, confere `hyprctl configerrors` por erros que mencionem o
arquivo editado, relê a regra com `findMonitorRuleLocation` e replaneja o texto
para confirmar os três campos. O backup permanece após sucesso e após rollback.

**Aceite real:** na máquina do usuário, `hdrcal integrate --output DP-2`
detectou a integração da W4 e respondeu "já integrado ... nenhuma alteração
necessária" (exit 0). Em config sintética, `--apply` escreveu o bloco, criou o
backup e a segunda execução não duplicou nada.

## Pré-condições (seção 8.2)

O preflight executa as cinco verificações de HDR, sempre reportando todas, e em
seguida a checagem de versão:

1. `HDR_OUTPUT_METADATA.eotf == 2` via KMS.
2. `max bpc == 10` via KMS **e** `currentFormat` contendo `2101010` via
   `hyprctl monitors -j`.
3. `render:cm_auto_hdr == 0`.
4. nenhum ICC ativo (varredura conservadora, ver acima).
5. `render:direct_scanout == 0`.
6. `hyprctl version` interpretável e **≥ 0.55**; acima de 0.56.2 passa com
   aviso, porque só a 0.56.2 foi auditada.

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
