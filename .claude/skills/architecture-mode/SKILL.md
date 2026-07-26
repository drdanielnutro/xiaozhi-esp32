---
name: architecture-mode
description: Desativa o proxy autônomo para uma sessão de arquitetura conduzida pelo proprietário.
disable-model-invocation: true
---

# Modo arquitetura

Execute:

`node "$CLAUDE_PROJECT_DIR/.claude/hooks/codex-decision-proxy.mjs" --disable`

Confirme que o status retornado por:

`node "$CLAUDE_PROJECT_DIR/.claude/hooks/codex-decision-proxy.mjs" --status`

é `architecture`.

Neste modo, faça perguntas ao proprietário quando uma decisão arquitetural ou
de negócio for necessária. Não trate o Codex como substituto do proprietário
para redefinir visão, escopo ou limites protegidos.
