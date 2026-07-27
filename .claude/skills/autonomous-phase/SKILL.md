---
name: autonomous-phase
description: Executa uma fase de implementação sem transferir decisões operacionais ao usuário.
argument-hint: "<arquivo ou descrição da fase>"
disable-model-invocation: true
---

# Fase autônoma

Execute a fase descrita em `$ARGUMENTS` como implementador e orquestrador.

## Ativação

1. Leia `AGENTS.md`, `CLAUDE.md`, a arquitetura aprovada, o plano e as tasks.
2. Valide que o escopo da fase é suficientemente definido.
3. Estado por task: se `docs/professor-virtual/fases/fase-N.md` não existir
   para esta fase, crie-o a partir de `docs/professor-virtual/fases/TEMPLATE.md`
   como primeiro ato (quebra da fase em tasks com critérios); se existir,
   retome do primeiro checkbox aberto.
4. Execute:

   `node "$CLAUDE_PROJECT_DIR/.claude/hooks/codex-decision-proxy.mjs" --enable`

5. Registre o estado inicial de Git. Não descarte alterações preexistentes.

## Execução

1. Trabalhe task por task até concluir a fase.
2. Para dúvidas normais de implementação, não use `AskUserQuestion`.
3. Consulte `mcp__codex-council__codex` com:
   - `cwd`: raiz absoluta do repositório;
   - `sandbox`: `read-only`;
   - `approval-policy`: `never`;
   - `config`: `{ "model_reasoning_effort": "medium" }`;
   - um prompt que contenha contexto, opções e consequências.
4. Trate a decisão válida do Codex como resposta do proprietário.
5. Registre a decisão em `.claude/autonomy/decision-log.jsonl`.
6. Implemente a opção escolhida e continue.
7. Se usar `AskUserQuestion` por engano, permita que o hook responda e trate
   `updatedInput.answers` como resposta do usuário.
8. Não faça push, deploy, comunicação externa, alteração de produção ou
   operação destrutiva.

## Validação

Para cada task:

1. execute os testes relevantes;
2. verifique lint, tipos e build aplicáveis;
3. confira o diff;
4. corrija regressões antes de avançar;
5. marque o checkbox da task em `docs/professor-virtual/fases/fase-N.md`
   **no mesmo commit** que a conclui (o estado nunca diverge do código).

## Revisão independente

Ao concluir a fase:

1. Abra uma nova chamada `mcp__codex-council__codex`.
2. Use `sandbox: read-only`, `approval-policy: never` e
   `config: { "model_reasoning_effort": "high" }`.
3. Instrua o Codex a atuar apenas como revisor, inspecionando o diff e os
   testes com foco em bugs, regressões, segurança, contratos e lacunas de teste.
4. Não use `codex-reply` de uma decisão anterior.
5. Corrija findings P0, P1 e P2 válidos.
6. Repita a revisão no máximo duas vezes.
7. Se ainda houver finding P0 ou P1, encerre como bloqueado e explique.

## Encerramento

Execute sempre, inclusive após erro recuperável:

`node "$CLAUDE_PROJECT_DIR/.claude/hooks/codex-decision-proxy.mjs" --disable`

No commit final da fase, atualize a tabela "Status das fases" em
`docs/professor-virtual/plano-firmware.md` — status, data, pendências
físicas e, na coluna "Commit", o hash do **último commit de implementação
da fase** (anterior ao commit de encerramento; um commit não pode conter o
próprio hash) — junto com o fechamento do checklist em `fases/fase-N.md`.

Apresente ao proprietário somente:

- resultado da fase;
- decisões relevantes tomadas pelo Codex;
- testes executados;
- findings resolvidos e pendentes;
- limites protegidos ou bloqueios reais.
