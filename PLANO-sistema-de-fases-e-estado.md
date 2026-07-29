# Plano — Estado por task + hooks de sessão e lembrete (aprimoramento do processo)

> **Status: executado integralmente** no commit `96d2128` (entregas 1–4 abaixo),
> após duas rodadas de revisão independente do Codex (6 findings corrigidos,
> listados na mensagem do commit). Evoluções posteriores do sistema estão em
> `PLANO-aprimoramentos-sistema-de-fases.md` (tasks 1–5, commits `55fa042`
> a `d2192fd`).
>
> Em 28/07/2026 esta especificação foi **retrofitada** com os 6 findings
> (B1–B6 de `CORRECOES-E-ANALISE-planos-pre-unificacao.md`): o texto abaixo
> agora descreve o comportamento do código commitado, incluindo o que as
> revisões corrigiram.

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
- Regra (documentada no template e no plano-firmware): o arquivo `fase-N.md` é criado como **primeiro ato da fase** (quebra em tasks); cada checkbox é marcado **no mesmo commit** que conclui a task — estado nunca diverge do código; ao encerrar a fase, atualizar a tabela macro do `plano-firmware.md` no commit final. Na coluna "Commit" da tabela entra o hash do **último commit de implementação da fase** — anterior ao commit de encerramento, pois um commit não pode conter o próprio hash.
- Ajustes de texto: `plano-firmware.md` (nota de retomada passa a citar `fases/fase-N.md`; processo transversal cita o checklist) e `.claude/skills/autonomous-phase/SKILL.md` (Ativação: criar/ler o arquivo da fase; Validação: marcar checkbox no commit da task; Encerramento: atualizar tabela de status).

### 2. Hook de SessionStart — `.claude/hooks/pv-session-context.mjs`

- Dispara em todo início de sessão (startup/resume/clear/compact — sem matcher restritivo).
- Imprime no stdout (vira contexto da sessão), de forma **curta**:
  1. a seção "## Status das fases" do `plano-firmware.md` (só a tabela);
  2. o checklist da fase em andamento (o `fases/fase-N.md` mais recente, se existir);
  3. última linha do `git log --oneline -1` + contagem de `git status --porcelain` (mudanças pendentes);
  4. uma linha lembrando o caminho do plano e do dossiê.
- Checklist da fase truncado em 60 linhas, com ponteiro para o arquivo completo. Robusto a ausência de arquivos (repo recém-clonado: imprime "sem fases iniciadas") e a erros: `try/catch` global degrada para "(estado parcialmente indisponível: …)" em vez de abortar; `git log` com fallback "sem commits". Sem dependências além de Node + git.

### 3. Hook de Stop — `.claude/hooks/pv-state-reminder.mjs`

Lógica (stateless quanto a ferramentas; a verdade vem do git):
1. Lê o JSON do evento no stdin; entrada ilegível, JSON inválido ou JSON válido porém não-objeto (ex.: `"null"`) → sai silencioso sem bloquear (um hook de Stop que quebra com entrada malformada bloquearia toda parada de sessão); se `stop_hook_active === true`, sai silencioso (anti-loop).
2. `git status --porcelain`: vazio → sai silencioso (nada a lembrar).
3. Assinatura **sensível a conteúdo** = hash de (HEAD + porcelain + `git diff HEAD` completo + tamanho/mtime de cada entrada `??` do porcelain). Assinatura por lista de arquivos não basta: concluir uma task editando os mesmos arquivos não geraria novo lembrete. Se igual à última gravada em `.claude/state/stop-reminder.json` → sai silencioso (já lembrou deste exato estado; evita fadiga de alarme).
4. Senão, **grava a assinatura antes de emitir o bloqueio**, com escrita atômica (arquivo `.tmp` + rename); se a persistência falhar, sai silencioso **sem bloquear** (sem deduplicação garantida, é preferível não lembrar a bloquear repetidamente pelo mesmo estado). Só então emite `{"decision":"block","reason":"<uma linha>"}` e **termina naturalmente** — nunca `process.exit()` logo após o write, que pode truncar o stdout antes do flush e neutralizar o bloqueio. Lembrete curto: "Há mudanças não commitadas. Se concluiu uma task, marque o checkbox em docs/professor-virtual/fases/ e commite junto; se a fase encerrou, atualize a tabela de status. Se o trabalho está no meio, apenas prossiga/encerre."
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
