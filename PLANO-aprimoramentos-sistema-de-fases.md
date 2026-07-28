# Plano — Aprimoramentos do sistema de fases e estado

> **Para agentes executores:** use a skill `superpowers:subagent-driven-development`
> (recomendada) ou `superpowers:executing-plans` para implementar este plano
> task a task. Os passos usam checkboxes (`- [ ]`) para rastreamento.

**Objetivo:** fortalecer o sistema atual de fases/tasks do Professor Virtual em
três pontos: retomada precisa após pausas no meio de uma fase, proteção da
janela de contexto do orquestrador em tasks pesadas, e verificação automática
de que o estado documentado nunca diverge do git.

**Arquitetura:** o sistema atual já tem (a) estado grosso na tabela "Status das
fases" de `docs/professor-virtual/plano-firmware.md`; (b) estado fino por task
em `docs/professor-virtual/fases/fase-N.md` (criado do `TEMPLATE.md`); (c) o
hook de SessionStart `pv-session-context.mjs`, que injeta esse estado em toda
sessão nova; (d) o hook de Stop `pv-state-reminder.mjs`, que lembra de marcar
checkboxes junto com o commit. Este plano **estende** essas peças sem alterar
seus contratos: acrescenta uma seção de pausa ao template (lida integralmente
pelo SessionStart), um validador de integridade estado ↔ git (novo script,
também acoplado ao SessionStart), e regras de despacho de tasks pesadas para
subagentes de contexto limpo na skill `autonomous-phase`.

**Stack:** Node.js ≥ 18 (já usado pelos hooks), `node:test` embutido para os
testes (sem dependências novas), git como fonte de verdade.

## Restrições globais

- Nenhuma dependência npm nova; apenas APIs embutidas do Node.
- Portabilidade macOS/Linux: caminhos sempre via `node:path`, nunca literais
  de plataforma.
- Não alterar os contratos existentes: SessionStart imprime texto puro no
  stdout; Stop imprime JSON `{decision, reason}`; o validador é novo e não
  tem efeitos colaterais (só lê).
- Todo texto voltado ao agente/proprietário em português.
- Mudanças restritas a `docs/professor-virtual/` e `.claude/` — nenhum
  arquivo de firmware é tocado.
- Testes em `.claude/hooks/tests/`, executados com
  `node --test .claude/hooks/tests/`.
- Commits pequenos, um por task deste plano. Sem `git push` (regra do repo).
- `.claude/state/` já está no `.gitignore`; não versionar nada dali.

---

### Task 1: Seção "Contexto de retomada" (template + skill)

Uma pausa no meio de uma task hoje se perde: o checklist diz *o que* falta,
mas não *onde exatamente* o trabalho parou. Esta task cria o lugar canônico
para esse registro.

**Arquivos:**
- Modificar: `docs/professor-virtual/fases/TEMPLATE.md`
- Modificar: `.claude/skills/autonomous-phase/SKILL.md`

**Interfaces:**
- Produz: a seção `## Contexto de retomada` com marcador literal `(vazio)`
  quando não preenchida — as Tasks 2 e o hook dependem desses dois textos
  exatos (`## Contexto de retomada` e `(vazio)`).

- [ ] **Passo 1: Acrescentar a seção ao template**

Em `docs/professor-virtual/fases/TEMPLATE.md`, inserir imediatamente antes
de `## Notas da fase`:

```markdown
## Contexto de retomada

> Preencha ao interromper o trabalho no meio de uma task (fim de sessão,
> pausa pedida pelo proprietário, bloqueio temporário). Substitua todo o
> conteúdo por "(vazio)" no commit que concluir a task retomada. O hook de
> SessionStart injeta esta seção integralmente na próxima sessão.

(vazio)

<!-- Formato sugerido ao preencher:
- Task em andamento: TN
- Último passo concluído: ...
- Próximo passo exato: ...
- Decisões já tomadas nesta task (data + resumo): ...
- Estado do worktree / armadilhas: ...
-->
```

- [ ] **Passo 2: Ensinar a skill a usar a seção**

Em `.claude/skills/autonomous-phase/SKILL.md`, no item 3 de "Ativação",
substituir:

```markdown
3. Estado por task: se `docs/professor-virtual/fases/fase-N.md` não existir
   para esta fase, crie-o a partir de `docs/professor-virtual/fases/TEMPLATE.md`
   como primeiro ato (quebra da fase em tasks com critérios); se existir,
   retome do primeiro checkbox aberto.
```

por:

```markdown
3. Estado por task: se `docs/professor-virtual/fases/fase-N.md` não existir
   para esta fase, crie-o a partir de `docs/professor-virtual/fases/TEMPLATE.md`
   como primeiro ato (quebra da fase em tasks com critérios); se existir,
   leia primeiro a seção "Contexto de retomada" (quando preenchida) para se
   reancorar no ponto exato da pausa, e então retome do primeiro checkbox
   aberto. Volte a seção para "(vazio)" no commit que concluir a task
   retomada.
```

E, ao final da lista de "Execução" (após o item 8), acrescentar:

```markdown
9. Ao interromper no meio de uma task (fim de sessão, pausa, bloqueio
   temporário), preencha a seção "Contexto de retomada" da `fase-N.md`
   antes de encerrar: task em andamento, último passo concluído, próximo
   passo exato e decisões já tomadas. Não é preciso commitar a pausa — o
   arquivo no worktree basta; o hook de SessionStart injeta a seção na
   sessão seguinte.
```

- [ ] **Passo 3: Verificar**

Releia os dois arquivos e confirme: o template tem a nova seção antes de
"Notas da fase" com o literal `(vazio)`; a skill referencia a seção na
ativação (retomada) e na execução (pausa), com os mesmos nomes exatos.

- [ ] **Passo 4: Commit**

```bash
git add docs/professor-virtual/fases/TEMPLATE.md .claude/skills/autonomous-phase/SKILL.md
git commit -m "Adiciona contexto de retomada ao template de fase e à skill"
```

---

### Task 2: SessionStart imprime o contexto de retomada integralmente

O hook `pv-session-context.mjs` trunca o checklist da fase em 60 linhas — uma
pausa registrada no fim do arquivo ficaria de fora. Esta task garante que a
seção de retomada seja sempre injetada por inteiro.

**Arquivos:**
- Modificar: `.claude/hooks/pv-session-context.mjs` (bloco do checklist,
  linhas ~63–72)
- Criar: `.claude/hooks/tests/pv-session-context.test.mjs`

**Interfaces:**
- Consome: a seção `## Contexto de retomada` e o literal `(vazio)` da Task 1.
- Produz: bloco `--- Contexto de retomada (pausa anterior) ---` no stdout do
  hook, apenas quando a seção está preenchida.

- [ ] **Passo 1: Escrever os testes (devem falhar)**

Criar `.claude/hooks/tests/pv-session-context.test.mjs`:

```javascript
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";

const HOOK = new URL("../pv-session-context.mjs", import.meta.url).pathname;

function makeProject() {
  const dir = mkdtempSync(join(tmpdir(), "pv-context-"));
  mkdirSync(join(dir, "docs", "professor-virtual", "fases"), {
    recursive: true,
  });
  return dir;
}

function run(dir) {
  return spawnSync(process.execPath, [HOOK], {
    cwd: dir,
    encoding: "utf8",
    env: { ...process.env, CLAUDE_PROJECT_DIR: dir },
  });
}

test("imprime o contexto de retomada mesmo com checklist além do corte", () => {
  const dir = makeProject();
  const recheio = Array.from({ length: 80 }, (_, i) => `linha ${i}`).join("\n");
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    `# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n\n${recheio}\n\n` +
      `## Contexto de retomada\n\n- Task em andamento: T1\n` +
      `- Próximo passo exato: implementar o parser\n\n## Notas da fase\n`
  );
  const r = run(dir);
  assert.equal(r.status, 0);
  assert.match(r.stdout, /Contexto de retomada \(pausa anterior\)/);
  assert.match(r.stdout, /Próximo passo exato: implementar o parser/);
});

test("não imprime a seção quando está vazia", () => {
  const dir = makeProject();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    `# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n\n` +
      `## Contexto de retomada\n\n(vazio)\n\n## Notas da fase\n`
  );
  const r = run(dir);
  assert.equal(r.status, 0);
  assert.doesNotMatch(r.stdout, /pausa anterior/);
});
```

- [ ] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test .claude/hooks/tests/`
Esperado: o primeiro teste FALHA (o hook ainda não imprime a seção); o
segundo passa por vacuidade.

- [ ] **Passo 3: Implementar no hook**

Em `.claude/hooks/pv-session-context.mjs`, duas edições dentro do bloco
`if (faseFile) { ... }` (comentário "2. Checklist da fase corrente"):

**(a)** Substituir a linha:

```javascript
  const lines = readFileSync(join(fasesDir, faseFile), "utf8").split("\n");
```

por:

```javascript
  const faseText = readFileSync(join(fasesDir, faseFile), "utf8");
  const lines = faseText.split("\n");
```

**(b)** Logo após o bloco de truncamento:

```javascript
  if (lines.length > MAX_FASE_LINES) {
    out.push(`(... truncado; leia docs/professor-virtual/fases/${faseFile})`);
  }
```

e ainda **dentro** do `if (faseFile)`, inserir:

```javascript
  // A retomada sai sempre integral: o corte de 60 linhas acima não pode
  // engolir justamente o registro de onde o trabalho parou.
  const retomada = faseText.match(
    /## Contexto de retomada[\s\S]*?(?=\n## |$)/
  );
  if (retomada && !retomada[0].includes("(vazio)")) {
    out.push("--- Contexto de retomada (pausa anterior) ---");
    out.push(retomada[0].trim());
  }
```

- [ ] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test .claude/hooks/tests/`
Esperado: 2 testes PASSAM.

- [ ] **Passo 5: Commit**

```bash
git add .claude/hooks/pv-session-context.mjs .claude/hooks/tests/pv-session-context.test.mjs
git commit -m "SessionStart injeta contexto de retomada integralmente"
```

---

### Task 3: Validador de integridade estado ↔ git

O hook de Stop lembra de atualizar o estado, mas nada *verifica* depois que a
regra foi cumprida. Este script audita as invariantes do sistema: fase
concluída na tabela ⇒ checklist fechado e hash real; checklist fechado ⇒
tabela atualizada; commit que marca checkbox ⇒ toca código (ou é o commit de
encerramento); checkbox marcado no worktree ⇒ aviso para commitar junto.

**Arquivos:**
- Criar: `.claude/hooks/pv-state-validate.mjs`
- Criar: `.claude/hooks/tests/pv-state-validate.test.mjs`

**Interfaces:**
- Consome: tabela "Status das fases" de `plano-firmware.md` (colunas
  `| Fase | Status | Concluída em | Commit | Pendências físicas |`) e os
  arquivos `fases/fase-N.md`.
- Produz: CLI `node .claude/hooks/pv-state-validate.mjs [--quiet]`.
  Saída `Integridade estado ↔ git: OK` e código 0 quando consistente
  (silêncio total com `--quiet`); relatório
  `Integridade estado ↔ git: PROBLEMAS` + lista e código 1 quando não.
  A Task 4 depende exatamente desse contrato.

- [ ] **Passo 1: Escrever os testes (devem falhar)**

Criar `.claude/hooks/tests/pv-state-validate.test.mjs`:

```javascript
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { execSync, spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";

const VALIDATOR = new URL("../pv-state-validate.mjs", import.meta.url)
  .pathname;

function makeRepo() {
  const dir = mkdtempSync(join(tmpdir(), "pv-validate-"));
  const g = (cmd) => execSync(cmd, { cwd: dir, encoding: "utf8" });
  g("git init -q");
  g('git config user.email "t@t" && git config user.name "t"');
  mkdirSync(join(dir, "docs", "professor-virtual", "fases"), {
    recursive: true,
  });
  return { dir, g };
}

function runValidator(dir, args = []) {
  return spawnSync(process.execPath, [VALIDATOR, ...args], {
    cwd: dir,
    encoding: "utf8",
    env: { ...process.env, CLAUDE_PROJECT_DIR: dir },
  });
}

test("repositório consistente sai com código 0", () => {
  const { dir, g } = makeRepo();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n"
  );
  g('git add -A && git commit -qm "abre fase 1"');
  const r = runValidator(dir);
  assert.equal(r.status, 0);
  assert.match(r.stdout, /OK/);
});

test("--quiet silencia a saída quando consistente", () => {
  const { dir, g } = makeRepo();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n"
  );
  g('git add -A && git commit -qm "abre fase 1"');
  const r = runValidator(dir, ["--quiet"]);
  assert.equal(r.status, 0);
  assert.equal(r.stdout, "");
});

test("acusa fase concluída na tabela com checkbox aberto", () => {
  const { dir, g } = makeRepo();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n- [x] T2 — leitor\n"
  );
  writeFileSync(
    join(dir, "docs", "professor-virtual", "plano-firmware.md"),
    "# Plano\n\n## Status das fases\n\n" +
      "| Fase | Status | Concluída em | Commit | Pendências físicas |\n" +
      "|---|---|---|---|---|\n" +
      "| F1 — Fundações | concluída | 2026-07-28 | abc1234 | — |\n"
  );
  writeFileSync(join(dir, "codigo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /checkbox\(es\) aberto\(s\)/);
});

test("acusa commit que marca checkbox sem tocar código", () => {
  const { dir, g } = makeRepo();
  const fase = join(dir, "docs", "professor-virtual", "fases", "fase-1.md");
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n");
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n");
  g('git add -A && git commit -qm "marca T1 sem codigo"');
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /marca checkbox sem tocar código/);
});

test("acusa checkbox marcado no worktree sem commit", () => {
  const { dir, g } = makeRepo();
  const fase = join(dir, "docs", "professor-virtual", "fases", "fase-1.md");
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n");
  g('git add -A && git commit -qm "abre fase 1"');
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n");
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /marcado\(s\) no worktree sem commit/);
});
```

- [ ] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test .claude/hooks/tests/`
Esperado: os 5 testes novos FALHAM ("Cannot find module ...
pv-state-validate.mjs"); os da Task 2 continuam passando.

- [ ] **Passo 3: Implementar o validador**

Criar `.claude/hooks/pv-state-validate.mjs`:

```javascript
#!/usr/bin/env node
// Validador de integridade estado ↔ git do Professor Virtual.
// Uso: node .claude/hooks/pv-state-validate.mjs [--quiet]
// Código 0 = consistente; 1 = problemas (listados no stdout).
// Somente leitura: nunca altera arquivo ou estado do git.

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { execSync } from "node:child_process";
import { resolve, join } from "node:path";

const root = resolve(process.env.CLAUDE_PROJECT_DIR || process.cwd());
const quiet = process.argv.includes("--quiet");
const planoPath = join(root, "docs", "professor-virtual", "plano-firmware.md");
const fasesDir = join(root, "docs", "professor-virtual", "fases");
const MAX_COMMITS = 50;
const problems = [];

function sh(cmd) {
  try {
    return execSync(cmd, {
      cwd: root,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return "";
  }
}

function listFases() {
  if (!existsSync(fasesDir)) return [];
  return readdirSync(fasesDir)
    .map((f) => {
      const m = f.match(/^fase-(\d+)\.md$/);
      return m ? { file: f, n: Number(m[1]) } : null;
    })
    .filter(Boolean)
    .sort((a, b) => a.n - b.n);
}

function checkboxes(text) {
  return {
    open: (text.match(/^- \[ \]/gm) || []).length,
    done: (text.match(/^- \[[xX]\]/gm) || []).length,
  };
}

function parseStatusTable() {
  if (!existsSync(planoPath)) return [];
  const plano = readFileSync(planoPath, "utf8");
  const section = plano.match(/## Status das fases[\s\S]*?(?=\n## |$)/);
  if (!section) return [];
  return section[0]
    .split("\n")
    .filter((l) => /^\|\s*F\d+/.test(l))
    .map((l) => {
      const cols = l.split("|").map((c) => c.trim());
      const m = (cols[1] || "").match(/^F(\d+)/);
      return m
        ? { n: Number(m[1]), status: cols[2] || "", commit: cols[4] || "" }
        : null;
    })
    .filter(Boolean);
}

const fases = listFases();
const rows = parseStatusTable();

// Invariante 1: fase concluída na tabela ⇒ checklist existente, fechado,
// e hash de commit real.
for (const row of rows) {
  if (!/conclu/i.test(row.status)) continue;
  const fasePath = join(fasesDir, `fase-${row.n}.md`);
  if (!existsSync(fasePath)) {
    problems.push(
      `F${row.n}: tabela diz "${row.status}", mas fases/fase-${row.n}.md não existe.`
    );
  } else {
    const { open } = checkboxes(readFileSync(fasePath, "utf8"));
    if (open > 0) {
      problems.push(
        `F${row.n}: tabela diz "${row.status}", mas há ${open} checkbox(es) aberto(s) em fase-${row.n}.md.`
      );
    }
  }
  const hash = (row.commit.match(/[0-9a-f]{7,40}/i) || [])[0];
  if (!hash) {
    problems.push(`F${row.n}: fase concluída sem hash de commit na tabela.`);
  } else if (sh(`git cat-file -t ${hash}`) !== "commit") {
    problems.push(
      `F${row.n}: hash "${hash}" da tabela não existe neste repositório.`
    );
  }
}

// Invariante 2: checklist 100% fechado ⇒ tabela deve refletir a conclusão.
for (const fase of fases) {
  const { open, done } = checkboxes(
    readFileSync(join(fasesDir, fase.file), "utf8")
  );
  if (done === 0 || open > 0) continue;
  const row = rows.find((r) => r.n === fase.n);
  if (row && !/conclu/i.test(row.status)) {
    problems.push(
      `F${fase.n}: checklist 100% fechado, mas tabela diz "${row.status}" — atualizar plano-firmware.md.`
    );
  }
}

// Invariante 3 (regra de ouro): commit que marca [x] deve tocar código no
// mesmo commit — exceto o commit de encerramento da fase, que toca o plano.
for (const fase of fases) {
  const rel = `docs/professor-virtual/fases/${fase.file}`;
  const hashes = sh(`git log --format=%H -n ${MAX_COMMITS} -- "${rel}"`)
    .split("\n")
    .filter(Boolean);
  for (const hash of hashes) {
    const diff = sh(`git show --format= --unified=0 ${hash} -- "${rel}"`);
    const marks = (diff.match(/^\+- \[[xX]\]/gm) || []).length;
    if (marks === 0) continue;
    const files = sh(`git show --format= --name-only ${hash}`)
      .split("\n")
      .filter(Boolean);
    const touchesCode = files.some(
      (f) => !f.startsWith("docs/") && !f.startsWith(".claude/")
    );
    const isClosure = files.includes(
      "docs/professor-virtual/plano-firmware.md"
    );
    if (!touchesCode && !isClosure) {
      problems.push(
        `F${fase.n}: commit ${hash.slice(0, 7)} marca checkbox sem tocar código nem encerrar a fase.`
      );
    }
  }
}

// Invariante 4: checkbox marcado no worktree sem commit.
const wtDiff = sh(
  'git diff HEAD --unified=0 -- "docs/professor-virtual/fases/"'
);
const wtMarks = (wtDiff.match(/^\+- \[[xX]\]/gm) || []).length;
if (wtMarks > 0) {
  problems.push(
    `Há ${wtMarks} checkbox(es) marcado(s) no worktree sem commit — commitar junto com o código da task.`
  );
}

if (problems.length === 0) {
  if (!quiet) process.stdout.write("Integridade estado ↔ git: OK\n");
  process.exit(0);
}
process.stdout.write(
  "Integridade estado ↔ git: PROBLEMAS\n" +
    problems.map((p) => `- ${p}`).join("\n") +
    "\n"
);
process.exit(1);
```

- [ ] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test .claude/hooks/tests/`
Esperado: 7 testes PASSAM (5 do validador + 2 da Task 2). Rodar também
`node .claude/hooks/pv-state-validate.mjs` na raiz do repo real e conferir
saída `OK` (ou investigar problemas reais apontados).

- [ ] **Passo 5: Commit**

```bash
git add .claude/hooks/pv-state-validate.mjs .claude/hooks/tests/pv-state-validate.test.mjs
git commit -m "Adiciona validador de integridade estado-git"
```

---

### Task 4: SessionStart reporta problemas de integridade

Com o validador pronto, toda sessão nova deve começar sabendo se o estado
documentado bate com o git — mas sem ruído quando está tudo certo.

**Arquivos:**
- Modificar: `.claude/hooks/pv-session-context.mjs` (imports e bloco final
  antes dos "Ponteiros permanentes")
- Modificar: `.claude/hooks/tests/pv-session-context.test.mjs`

**Interfaces:**
- Consome: o contrato CLI da Task 3 (`--quiet`: silêncio + código 0 quando
  OK; relatório + código 1 quando não).

- [ ] **Passo 1: Escrever o teste (deve falhar)**

Acrescentar ao final de `.claude/hooks/tests/pv-session-context.test.mjs`
(reutilizando `makeProject` e `run` já definidos; o fixture precisa de git
porque o validador o consulta):

```javascript
test("reporta problemas de integridade na abertura da sessão", () => {
  const dir = makeProject();
  const g = (cmd) =>
    spawnSync("sh", ["-c", cmd], { cwd: dir, encoding: "utf8" });
  g("git init -q");
  g('git config user.email "t@t" && git config user.name "t"');
  const fase = join(dir, "docs", "professor-virtual", "fases", "fase-1.md");
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n");
  g('git add -A && git commit -qm "abre fase 1"');
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n");
  const r = run(dir);
  assert.equal(r.status, 0);
  assert.match(r.stdout, /Integridade estado ↔ git: PROBLEMAS/);
});
```

- [ ] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test .claude/hooks/tests/`
Esperado: o teste novo FALHA (o hook ainda não chama o validador).

- [ ] **Passo 3: Implementar a chamada no hook**

Em `.claude/hooks/pv-session-context.mjs`, trocar o import:

```javascript
import { execSync } from "node:child_process";
```

por:

```javascript
import { execSync, spawnSync } from "node:child_process";
```

e inserir, entre o bloco "3. Git em uma linha" e o bloco
"4. Ponteiros permanentes":

```javascript
// 3b. Integridade estado ↔ git: só aparece quando há problemas.
const validate = spawnSync(
  process.execPath,
  [join(root, ".claude", "hooks", "pv-state-validate.mjs"), "--quiet"],
  {
    cwd: root,
    encoding: "utf8",
    timeout: 10000,
    env: { ...process.env, CLAUDE_PROJECT_DIR: root },
  }
);
if (validate.status !== 0 && validate.stdout) {
  out.push(validate.stdout.trim());
}
```

- [ ] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test .claude/hooks/tests/`
Esperado: 8 testes PASSAM. Atenção ao teste "não imprime a seção quando está
vazia": o fixture dele não tem git, o validador retorna problemas vazios?
Não — sem git, todos os comandos `sh()` do validador retornam `""`, nenhum
problema é acumulado e ele sai com 0; o hook não acrescenta nada. Se esse
teste quebrar, é regressão real.

- [ ] **Passo 5: Commit**

```bash
git add .claude/hooks/pv-session-context.mjs .claude/hooks/tests/pv-session-context.test.mjs
git commit -m "SessionStart reporta integridade estado-git na abertura"
```

---

### Task 5: Tasks pesadas em subagentes de contexto limpo (skill)

Fases longas degradam a janela de contexto do orquestrador: depois de várias
tasks, sobra menos espaço para raciocinar e a qualidade cai em silêncio. A
mitigação é despachar a implementação de tasks volumosas para subagentes que
nascem com contexto limpo, mantendo o orquestrador leve — só plano, decisões
e verificação.

**Arquivos:**
- Modificar: `.claude/skills/autonomous-phase/SKILL.md`

**Interfaces:**
- Consome: o fluxo de decisão via `mcp__codex-council__codex` já descrito na
  skill (as dúvidas dos subagentes voltam por esse caminho).

- [ ] **Passo 1: Acrescentar a subseção à skill**

Em `.claude/skills/autonomous-phase/SKILL.md`, inserir entre as seções
"## Execução" e "## Validação":

```markdown
### Tasks pesadas em contexto limpo

Para proteger a janela de contexto do orquestrador em fases longas, execute
em um subagente (ferramenta de agente, tipo `general-purpose`) toda task que
provavelmente exigir ler muitos arquivos do firmware ou produzir um diff
grande (heurística: mais de ~5 arquivos tocados ou mais de ~300 linhas de
mudança previstas).

1. O prompt do subagente deve ser autossuficiente: texto da task e critério
   "pronto quando" copiados da `fase-N.md`; arquivos e diretórios de
   partida; invariantes aplicáveis do `AGENTS.md`; e as proibições — não
   commitar, não fazer push, não editar arquivos gerados/vendor, não gravar
   em hardware.
2. O subagente implementa e testa, mas NÃO decide dúvidas materiais
   (arquitetura, biblioteca, contrato, nomenclatura pública): ao encontrar
   uma, deve parar e retornar a dúvida com as opções e consequências. O
   orquestrador então decide pelo fluxo normal (`mcp__codex-council__codex`)
   e redespacha o subagente com a decisão tomada.
3. Ao receber o resultado, o orquestrador confere o diff, roda os testes
   relevantes e só então marca o checkbox e commita (seção "Validação").
   O commit é sempre do orquestrador — nunca do subagente.
4. Uma task por subagente. Não paralelize tasks com dependência entre si ou
   que toquem os mesmos arquivos.
```

- [ ] **Passo 2: Verificar a coerência da skill**

Releia a skill inteira e confirme: a numeração das seções continua íntegra;
a subseção nova não contradiz "Execução" (decisões continuam passando pelo
Codex) nem "Validação" (checkbox no mesmo commit, feito pelo orquestrador).

- [ ] **Passo 3: Commit**

```bash
git add .claude/skills/autonomous-phase/SKILL.md
git commit -m "Skill de fase autonoma despacha tasks pesadas a subagentes"
```

---

## Critérios de aceite do plano

- [ ] `node --test .claude/hooks/tests/` passa com 8 testes verdes.
- [ ] `node .claude/hooks/pv-state-validate.mjs` na raiz do repo responde
      `Integridade estado ↔ git: OK` (ou os problemas apontados são reais e
      foram tratados).
- [ ] `node .claude/hooks/pv-session-context.mjs` continua imprimindo o
      bloco de estado normal, sem erro, em repo limpo.
- [ ] `TEMPLATE.md` e `SKILL.md` referenciam os mesmos literais
      (`## Contexto de retomada`, `(vazio)`).
- [ ] Cinco commits pequenos, um por task; push feito somente pelo
      proprietário.
