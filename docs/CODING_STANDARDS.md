# Padrões de código do hdrcal

Versão 1.0 — 2026-10-19. Este documento versiona as regras da seção 3–7 das
instruções do projeto. Mudanças exigem entrada no `CHANGELOG.md`.

## Linguagem e compilação

- C++20, sem extensões de compilador (`CMAKE_CXX_EXTENSIONS OFF`), sem código
  estilo-C quando existir equivalente idiomático.
- Compilação de referência: `-Wall -Wextra -Wpedantic -Werror`. Zero warnings.
- Todo arquivo passa por `clang-format` com o `.clang-format` do repositório.

## RAII e erros

- Nenhum `new`/`delete`; nenhum handle cru (arquivo, DRM, Wayland, Vulkan) sem
  wrapper com destrutor.
- Nenhuma leak em caminho de erro. Recursos temporários são fechados/removidos
  em todos os retornos.
- Mensagens de erro usam `std::optional<std::string>` ou um struct de resultado
  com um `optional<string>`; não usar out-params.
- `[[nodiscard]]` em toda função cujo resultado ignorado seria um bug.
- Entrada por `const&`, retorno por valor.
- `enum class`; sem macros funcionais quando `constexpr`/`inline` resolvem.
- `auto` apenas quando o tipo é óbvio na mesma linha.

## Nomenclatura

- **Tipos**: PascalCase, substantivos do domínio (`CalibrationProfile`,
  `MonitorOsdSnapshot`, `PatternPhase`). Nunca `Data`, `Info`, `Ctx`, `Obj`.
- **Funções/métodos**: verbo + objeto, camelCase
  (`computeLuminanceFromPqCode`, `saveProfileToPath`,
  `findProfileValidationError`, `applyTemporaryCalibrationRule`). Nunca
  `doProcess`, `handle()`, `check`, `run2`.
- **Variáveis**: nome completo do que representam
  (`maximumFullFrameLuminanceNits`, não `maxFF`, `v`, `tmp`, `aux`).
- **Constantes**: `k` + PascalCase, com unidade no nome
  (`kPqMaximumDisplayLuminanceNits`, `kTenBitMaximumSample`).
- **Arquivos**: snake_case casando o símbolo principal
  (`profile.hpp`/`profile.cpp`).
- Proibido: abreviaturas que exijam dicionário; single-letter exceto índice de
  loop local e óbvio; nome que mente sobre o que a função faz.

## Documentação

- Cada arquivo abre com comentário explicando seu papel no sistema.
- Cada declaração pública documenta **o que** faz e **por que**, com unidades
  (nits, código 0..1, amostra 0..1023), domínio e comportamento em erro.
- Docs do projeto em pt-BR; identificadores, comentários de código e commits em
  inglês.
- Documentos obrigatórios: `README.md`, `docs/CODING_STANDARDS.md`,
  `docs/DEVELOPMENT_PLAN.md`, `docs/DESIGN_NOTES.md`, `CHANGELOG.md`.
- Correção de erro anterior é registrada com visibilidade no CHANGELOG e/ou
  DESIGN_NOTES; histórico não é apagado silenciosamente.

## Testes

- Toda função de lógica nova nasce com teste unitário no mesmo commit.
- Nenhum teste exige GPU, compositor ou interação humana.
- Framework mínimo; macros de assert próprias em `tests/test_framework.hpp`.
- `ctest` verde é gate. Ao final de cada entrega, colar o comando executado e o
  resultado real.
- Nomes descrevem comportamento:
  `testComputeLuminanceFromPqCode_KnownAnchors_MatchesStd2084`.
- Bordas obrigatórias: zero, máximo, clamp, roundtrip.

## Sem código lixo

- Sem stub `TODO` commitado: a função existe completa ou existe só no plano.
- Sem código morto, comentado "para depois", função sem chamador ou abstração
  com uma única implementação hipotética (YAGNI).

## Idiomas

- Respostas ao usuário, docs e strings de UI: pt-BR.
- Identificadores, comentários de código e commits: inglês.
- Tom direto, técnico, sem bajulação.
