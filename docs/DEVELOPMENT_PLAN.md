# Plano de desenvolvimento do hdrcal

Versão 1.0 — 2026-10-19. Expandido a partir da seção 9 das instruções do
projeto. Cada milestone só começa depois do anterior estar aceito, salvo pedido
explícito do usuário.

## Stack

- **M0–M2 (MVP)**: mpv + libplacebo controlado por IPC.
- **M3+**: app nativo Vulkan/Wayland.
- O núcleo (PQ, perfil, Hyprland, KMS) é independente do backend gráfico.

## M0 — Spike do pipeline HDR

**Objetivo:** provar, antes de qualquer feature, que uma superfície taggeada PQ
faz `eotf=2`/`max bpc=10` aparecerem enquanto roda, e que um padrão em código
712/1023 (~600 nits) é visivelmente mais claro que 653/1023 (~350 nits) com
`max_luminance=10000`.

**Critérios de aceite:**

1. mpv/libplacebo escolhe superfície PQ 10-bit.
2. KMS reports `HDR_OUTPUT_METADATA.eotf == 2` e `max bpc == 10` durante a
   execução.
3. O usuário confirma visualmente a diferença entre 712 e 653.
4. Resultado registrado em `docs/DESIGN_NOTES.md`.

**Estado:** concluído em 2026-10-19. Evidências em `docs/DESIGN_NOTES.md`.

## M1 — Núcleo independente de GUI

**Objetivo:** implementar e testar toda a lógica que não depende de desenhar
padrões.

**Entregas:**

- `pq`: EOTF/OETF ST 2084, amostras 10-bit, tabela de âncoras permanente.
- `profile`: modelo `{output, min, max, avg, timestamp, osdSnapshot}`,
  validação, renderização Lua locale-independent, escrita atômica (tmp +
  `fsync` + `rename`).
- `hypr`: wrapper de `hyprctl` (monitors JSON, reload, regra temporária com
  restauração garantida), injeção de comando fake nos testes.
- `kms`: leitura read-only de `max bpc` e `HDR_OUTPUT_METADATA`, com seleção
  de card que reprova ambiguidade multi-GPU.
- `preflight`: as cinco pré-condições da seção 8.2 e checagem de versão do
  Hyprland (≥ 0.55).
- CLI: `profile write`, `preflight`, `probe`.

**Critérios de aceite:**

- `ctest` verde;
- `hdrcal profile write --output DP-2 --min 0.02 --max 600 --avg 350` gera
  `hdrcal.lua` correto;
- nenhum teste exige GPU/compositor.

**Estado:** concluído em 2026-10-19. Correções de portabilidade e segurança da
regra temporária (P1–P3) aplicadas no mesmo dia; detalhes no `CHANGELOG.md` e
em `docs/DESIGN_NOTES.md`.

## M2 — Wizard com backend mpv

**Objetivo:** conduzir os três testes visuais e persistir o resultado.

**Entregas:**

- gerador de padrões 16-bit BT.2020/PQ (implementado no próprio hdrcal com zlib
  e chunk `cICP`; a invocação de ffmpeg por passo foi rejeitada por latência e
  por não gravar `cICP` — ver `DESIGN_NOTES`);
- IPC do mpv (`--target-colorspace-hint=yes`);
- três fases (minimum luminance, maximum luminance, maximum full-frame
  luminance) com ←/→ (passo 1 código), Shift para passo grosso, Enter salva,
  Esc aborta;
- regra temporária aplicada antes da primeira fase e restaurada em sucesso,
  cancelamento, SIGINT/SIGTERM e watchdog pós-crash;
- gravação do perfil, `hyprctl reload`, exibição dos valores para colar em
  jogos.

**Critérios de aceite:**

- usuário completa os três testes;
- o perfil sobrevive a reboot/reload;
- restauração confirmada em todos os caminhos de saída.

**Estado:** implementado e smoke-testado em 2026-10-19. A mecânica foi
validada ao vivo (três fases via teclas do mpv, perfil gravado, reload, `Esc`,
SIGTERM e watchdog matando mpv órfão). O aceite final depende de uma sessão
visual real do usuário e da integração explícita em `monitors.lua` (o projeto
não altera essa configuração sem pedido).

## M3 — App nativo Vulkan/Wayland

**Objetivo:** paridade com M2 sem mpv.

**Entregas:**

- swapchain 10-bit (`A2B10G10R10_UNORM`);
- `VK_COLOR_SPACE_HDR10_ST2084_EXT` (confirmado disponível na RX 9070 XT/RADV
  do usuário);
- fullscreen edgeless;
- `wp-color-management-v1` via `wayland-scanner`;
- re-tag após mudanças do compositor.

**Critério de aceite:** paridade funcional com M2.

**Estado:** pendente.

## M4 — Qualidade

**Entregas:**

- snapshot e comparação DDC/CI (`ddcutil`: brilho, contraste, preset);
- aviso na re-verificação quando o OSD divergir do perfil;
- export JSON do perfil;
- substituição do toggle `sed` existente do usuário pelo toggle baseado em
  perfil.

**Estado:** pendente. O modelo de perfil e a CLI do M1 já aceitam o snapshot
opcional do OSD.

## M5 — Release interno

**Entregas:**

- README final com limitações;
- teste A/B de reprodutibilidade (2 sessões, delta esperado < ~10%);
- documentação das causas de variação (ABL, local dimming, temperatura).

**Estado:** pendente.
