---
description: Política de autonomia operacional com Codex como proxy de decisão
paths:
  - "**/*"
---

# Autonomia de implementação

Quando `.claude/autonomy/mode.json` contiver `"mode": "implementation"`:

1. Continue executando as tasks da fase até concluir, encontrar um limite
   protegido ou sofrer uma falha técnica não recuperável.
2. Não peça ao usuário escolhas normais de implementação.
3. Para cada dúvida material, chame `mcp__codex-council__codex` com:
   - `cwd`: raiz absoluta do repositório;
   - `sandbox`: `read-only`;
   - `approval-policy`: `never`;
   - `config`: `{ "model_reasoning_effort": "medium" }`.
4. Informe ao Codex a dúvida, as opções, o contexto técnico, a arquitetura
   aprovada e a consequência de cada opção.
5. Adote a resposta do Codex como decisão do usuário e prossiga.
6. Use `codex-reply` apenas para esclarecer a mesma decisão. Para uma decisão
   diferente ou para revisão, inicie uma nova chamada `codex`.
7. Não revele segredos ao Codex nem os grave no log.
8. Se o Codex classificar a questão como limite protegido, pare a fase e
   apresente ao usuário uma pergunta única, com recomendação e consequências.
9. Se o MCP falhar, tente uma vez novamente. Se ainda falhar, o hook de
   `AskUserQuestion` usará `codex exec` como caminho alternativo.

A resposta mais segura e reversível deve ser escolhida quando as opções forem
equivalentes ou incompletas. A falta de uma opção perfeita não justifica
interromper o usuário.
