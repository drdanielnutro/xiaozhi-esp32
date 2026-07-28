# Plano — Estado por task + hooks de sessão e lembrete (aprimoramento do processo)

> **Status: executado integralmente** no commit `96d2128` (entregas 1–4 abaixo),
> após duas rodadas de revisão independente do Codex (6 findings corrigidos).
> Evoluções posteriores do sistema estão em
> `PLANO-aprimoramentos-sistema-de-fases.md` (tasks 1–5, commits `55fa042`
> a `d2192fd`).

## Contexto

Antes de iniciar a F0 do firmware, o dono aprovou três melhorias de processo discutidas em conversa: (1) estado de progresso no nível de **task** (não só de fase), atualizado atomicamente com o commit de cada task; (2) hook de **SessionStart** que injeta o estado atual no meu contexto em toda sessão nova (sobrevive a `/clear`); (3) hook de **Stop** que lembra de atualizar o estado — condicionado ao **git** (worktree sujo), não a flags de uso de ferramenta, com deduplicação para não virar ruído. Filosofia (mesma do proxy): prompts orientam, hooks impõem.

Estado atual relevante:
- `.claude/settings.json` tem só o hook `PreToolUse`→`AskUserQuestion` (proxy). Hooks novos entram por merge, sem tocar no existente.
- Padrão de hook do projeto: Node/`.mjs` em `.claude/hooks/` (como `codex-decision-proxy.mjs`).
- `docs/professor-virtual/plano-firmware.md` já tem a tabela "Status das fases" (nível macro) e a nota de retomada.
- `.claude/skills/autonomous-phase/SKILL.md` rege a execução de fases — é onde a disciplina de checklist se torna obrigatória.

## Entregas

### 1. Convenção de estado por task — `docs/professor-virtual/fases/`

- Criar diretório com `TEMPLATE.md`: cabeçalho da fase (objetivo, critério "pronto quando", pendências físicas) + checklist de tasks (`- [ ] T1 — ...`), cada uma com critério curto de conclusão.
- Regra (documentada no template e no plano-firmware): o arquivo `fase-N.md` é criado como **primeiro ato da fase** (quebra em tasks); cada checkbox é marcado **no mesmo commit** que conclui a task — estado nunca diverge do código; ao encerrar a fase, atualizar a tabela macro do `plano-firmware.md` no commit final.
- Ajustes de texto: `plano-firmware.md` (nota de retomada passa a citar `fases/fase-N.md`; processo transversal cita o checklist) e `.claude/skills/autonomous-phase/SKILL.md` (Ativação: criar/ler o arquivo da fase; Validação: marcar checkbox no commit da task; Encerramento: atualizar tabela de status).

### 2. Hook de SessionStart — `.claude/hooks/pv-session-context.mjs`

- Dispara em todo início de sessão (startup/resume/clear/compact — sem matcher restritivo).
- Imprime no stdout (vira contexto da sessão), de forma **curta**:
  1. a seção "## Status das fases" do `plano-firmware.md` (só a tabela);
  2. o checklist da fase em andamento (o `fases/fase-N.md` mais recente, se existir);
  3. última linha do `git log --oneline -1` + contagem de `git status --porcelain` (mudanças pendentes);
  4. uma linha lembrando o caminho do plano e do dossiê.
- Robusto a ausência de arquivos (repo recém-clonado: imprime "sem fases iniciadas"). Sem dependências além de Node + git.

### 3. Hook de Stop — `.claude/hooks/pv-state-reminder.mjs`

Lógica (stateless quanto a ferramentas; a verdade vem do git):
1. Lê o JSON do evento no stdin; se `stop_hook_active === true`, sai silencioso (anti-loop).
2. `git status --porcelain`: vazio → sai silencioso (nada a lembrar).
3. Assinatura = hash de (porcelain + HEAD). Se igual à última gravada em `.claude/state/stop-reminder.json` → sai silencioso (já lembrou deste exato estado; evita fadiga de alarme).
4. Senão, grava a assinatura e emite `{"decision":"block","reason":"<uma linha>"}` — lembrete curto: "Há mudanças não commitadas. Se concluiu uma task, marque o checkbox em docs/professor-virtual/fases/ e commite junto; se a fase encerrou, atualize a tabela de status. Se o trabalho está no meio, apenas prossiga/encerre."
- Segurança do loop: após o lembrete, ou eu commito (porcelain esvazia → passa) ou paro de novo com o mesmo estado (assinatura igual → passa). Nunca bloqueia duas vezes seguidas pelo mesmo motivo.
- `.gitignore`: adicionar `.claude/state/` (estado local, como `mode.json`).

### 4. Registro no `.claude/settings.json` (merge, preservando o proxy)

```json
"SessionStart": [ { "hooks": [ { "type": "command", "command": "node \"$CLAUDE_PROJECT_DIR\"/.claude/hooks/pv-session-context.mjs" } ] } ],
"Stop":         [ { "hooks": [ { "type": "command", "command": "node \"$CLAUDE_PROJECT_DIR\"/.claude/hooks/pv-state-reminder.mjs" } ] } ]
```

## Verificação

1. `node --check` nos dois scripts; JSONs parseáveis.
2. **SessionStart** (simulável): executar o script direto e conferir a saída (tabela + fase + git). Teste real: o dono roda `/clear` e confirma que o estado aparece no início da sessão.
3. **Stop** (simulável por pipe, como fizemos no smoke test do proxy):
   - worktree sujo + `stop_hook_active:false` → JSON com `decision:"block"` e reason curta;
   - repetir com mesmo estado → silêncio (deduplicação);
   - `stop_hook_active:true` → silêncio;
   - worktree limpo → silêncio.
4. `/hooks` deve listar os três hooks (PreToolUse, SessionStart, Stop) — validação do dono na sessão.
5. Revisão independente do Codex (sessão nova, read-only) sobre os dois scripts; corrigir findings (máx. 2 rodadas).
6. Commit único do pacote (push do dono). Sem tocar em nada do firmware.

## Fora de escopo

- Nenhuma mudança no hook do proxy, no firmware ou no backend.
- O hook de Stop não commita nem edita nada sozinho — só lembra; a ação continua sendo minha/do dono.
