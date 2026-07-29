# Plano — Aprimoramentos do sistema de fases e estado

> **Para agentes executores:** implemente este plano task a task. Se as skills
> `superpowers:subagent-driven-development` ou `superpowers:executing-plans`
> existirem no ambiente, use-as; caso contrário, execute diretamente (com
> subagentes padrão quando útil). Os passos usam checkboxes (`- [ ]`) para
> rastreamento.

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
  `node --test ".claude/hooks/tests/*.test.mjs"`.
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
- Produz: a seção `## Contexto de retomada`. Contrato de "vazia": o **corpo
  útil** da seção — o que sobra fora do blockquote de instrução e dos
  comentários HTML — é exatamente `(vazio)`. A Task 2 depende desse contrato
  e dos literais `## Contexto de retomada` e `(vazio)`. Atenção: o literal
  `(vazio)` também aparece dentro do blockquote de instrução, portanto a
  detecção **nunca** pode ser por simples presença da string na seção.

- [x] **Passo 1: Acrescentar a seção ao template**

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

- [x] **Passo 2: Ensinar a skill a usar a seção**

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

- [x] **Passo 3: Verificar**

Releia os dois arquivos e confirme: o template tem a nova seção antes de
"Notas da fase" com o literal `(vazio)`; a skill referencia a seção na
ativação (retomada) e na execução (pausa), com os mesmos nomes exatos.

- [x] **Passo 4: Commit**

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
- Consome: a seção `## Contexto de retomada` da Task 1, pelo contrato de
  corpo útil (fora de blockquote/comentário HTML) igual a `(vazio)` = vazia.
- Produz: bloco `--- Contexto de retomada (pausa anterior) ---` no stdout do
  hook, apenas quando a seção está preenchida (corpo útil, sem blockquote
  nem comentários).

- [x] **Passo 1: Escrever os testes (devem falhar)**

Criar `.claude/hooks/tests/pv-session-context.test.mjs`:

```javascript
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";

const HOOK = fileURLToPath(
  new URL("../pv-session-context.mjs", import.meta.url)
);

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

// Formato real do template: o blockquote de instrução contém o literal
// "(vazio)" — os fixtures precisam incluí-lo para que os testes guardem a
// detecção por corpo útil (e não por simples presença da string).
const INSTRUCAO =
  `> Preencha ao interromper o trabalho no meio de uma task. Substitua todo\n` +
  `> o conteúdo por "(vazio)" no commit que concluir a task retomada.\n`;
const COMENTARIO =
  `<!-- Formato sugerido ao preencher:\n- Task em andamento: TN\n-->\n`;

test("imprime o contexto de retomada mesmo com checklist além do corte", () => {
  const dir = makeProject();
  const recheio = Array.from({ length: 80 }, (_, i) => `linha ${i}`).join("\n");
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    `# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n\n${recheio}\n\n` +
      `## Contexto de retomada\n\n${INSTRUCAO}\n` +
      `- Task em andamento: T1\n` +
      `- Próximo passo exato: implementar o parser\n\n` +
      `${COMENTARIO}\n## Notas da fase\n`
  );
  const r = run(dir);
  assert.equal(r.status, 0);
  assert.match(r.stdout, /Contexto de retomada \(pausa anterior\)/);
  assert.match(r.stdout, /Próximo passo exato: implementar o parser/);
  assert.doesNotMatch(r.stdout, /Preencha ao interromper/);
});

test("não imprime a seção quando está vazia", () => {
  const dir = makeProject();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    `# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n\n` +
      `## Contexto de retomada\n\n${INSTRUCAO}\n(vazio)\n\n` +
      `${COMENTARIO}\n## Notas da fase\n`
  );
  const r = run(dir);
  assert.equal(r.status, 0);
  assert.doesNotMatch(r.stdout, /pausa anterior/);
  // A seção é excisada do dump do checklist: nem o título, nem a instrução,
  // nem "(vazio)" devem aparecer no stdout quando está vazia.
  assert.doesNotMatch(r.stdout, /Contexto de retomada/);
  assert.doesNotMatch(r.stdout, /Preencha ao interromper/);
});
```

- [x] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: os dois testes FALHAM — o hook ainda despeja a seção de retomada
(título e instrução) no dump do checklist e não imprime o bloco dedicado.

- [x] **Passo 3: Implementar no hook**

Em `.claude/hooks/pv-session-context.mjs`, substituir o bloco
`if (faseFile) { ... }` (comentário "2. Checklist da fase corrente") pelo
bloco abaixo, **preservando o `else` que o segue**:

```javascript
if (faseFile) {
  const faseText = readFileSync(join(fasesDir, faseFile), "utf8");
  // A seção de retomada sai do dump do checklist: ela é tratada no bloco
  // dedicado abaixo — assim nem as instruções/"(vazio)" poluem o stdout,
  // nem o conteúdo preenchido aparece duas vezes.
  const retomada = faseText.match(
    /## Contexto de retomada[\s\S]*?(?=\n## |$)/
  );
  const checklistText = retomada
    ? faseText.replace(retomada[0], "")
    : faseText;
  const lines = checklistText.split("\n");
  out.push(`--- Checklist da fase corrente (${faseFile}) ---`);
  out.push(lines.slice(0, MAX_FASE_LINES).join("\n"));
  if (lines.length > MAX_FASE_LINES) {
    out.push(`(... truncado; leia docs/professor-virtual/fases/${faseFile})`);
  }
  // A retomada sai sempre integral e sanitizada: o corte de 60 linhas acima
  // não pode engolir justamente o registro de onde o trabalho parou. A
  // decisão usa o CORPO ÚTIL da seção (sem blockquote e sem comentários
  // HTML): o literal "(vazio)" também existe dentro da instrução do
  // template e não pode contar como conteúdo.
  if (retomada) {
    const corpo = retomada[0]
      .replace(/^## Contexto de retomada\s*/, "")
      .replace(/<!--[\s\S]*?-->/g, "")
      .split("\n")
      .filter((l) => !l.trim().startsWith(">"))
      .join("\n")
      .trim();
    if (corpo && corpo !== "(vazio)") {
      out.push("--- Contexto de retomada (pausa anterior) ---");
      out.push(corpo);
    }
  }
}
```

- [x] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: 2 testes PASSAM.

- [x] **Passo 5: Commit**

```bash
git add .claude/hooks/pv-session-context.mjs .claude/hooks/tests/pv-session-context.test.mjs
git commit -m "SessionStart injeta contexto de retomada integralmente"
```

---

### Task 3: Validador de integridade estado ↔ git

O hook de Stop lembra de atualizar o estado, mas nada *verifica* depois que a
regra foi cumprida. Este script audita as invariantes do sistema: sem git ⇒
acusa em vez de fingir OK; fase concluída na tabela ⇒ checklist fechado e
hash real; checklist fechado ⇒ tabela atualizada (inclusive quando a linha da
fase nem existe); commit que marca checkbox (contagem **líquida** — reescrever
uma linha já marcada não conta) ⇒ inclui outro arquivo de trabalho no mesmo
commit, seja código, doc entregue ou tooling (ou é o commit de encerramento);
checkbox marcado no worktree ou em fase não rastreada ⇒ aviso para commitar
junto.

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

- [x] **Passo 1: Escrever os testes (devem falhar)**

Criar `.claude/hooks/tests/pv-state-validate.test.mjs`:

```javascript
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { execSync, spawnSync } from "node:child_process";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";

const VALIDATOR = fileURLToPath(
  new URL("../pv-state-validate.mjs", import.meta.url)
);

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

test("acusa commit que só marca checkbox, sem arquivo de trabalho", () => {
  const { dir, g } = makeRepo();
  const fase = join(dir, "docs", "professor-virtual", "fases", "fase-1.md");
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [ ] T1 — parser\n");
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n");
  g('git add -A && git commit -qm "marca T1 sem trabalho junto"');
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /sem nenhum outro arquivo de trabalho/);
});

test("não acusa reformulação de linha já marcada (líquido zero)", () => {
  const { dir, g } = makeRepo();
  const fase = join(dir, "docs", "professor-virtual", "fases", "fase-1.md");
  writeFileSync(
    fase,
    "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n- [ ] T2 — leitor\n"
  );
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  writeFileSync(
    fase,
    "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser de linhas\n- [ ] T2 — leitor\n"
  );
  g('git add -A && git commit -qm "reformula texto de task concluida"');
  const r = runValidator(dir);
  assert.equal(r.status, 0);
});

test("acusa ausência de git em vez de fingir OK", () => {
  const dir = mkdtempSync(join(tmpdir(), "pv-validate-"));
  mkdirSync(join(dir, "docs", "professor-virtual", "fases"), {
    recursive: true,
  });
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /não é um repositório git/);
});

test("acusa checklist fechado sem linha na tabela", () => {
  const { dir, g } = makeRepo();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n"
  );
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /não há linha F1 na tabela/);
});

test("acusa checkbox marcado em fase não rastreada", () => {
  const { dir, g } = makeRepo();
  writeFileSync(join(dir, "README.md"), "x\n");
  g('git add -A && git commit -qm "inicial"');
  writeFileSync(
    join(dir, "docs", "professor-virtual", "fases", "fase-1.md"),
    "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n- [ ] T2 — leitor\n"
  );
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /arquivo não rastreado/);
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

- [x] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: os 9 testes novos FALHAM ("Cannot find module ...
pv-state-validate.mjs"); os da Task 2 continuam passando.

- [x] **Passo 3: Implementar o validador**

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

// Invariante 0: sem git não há auditoria — reportar em vez de fingir OK
// (sh() devolve "" tanto para "sem histórico" quanto para "git falhou").
if (sh("git rev-parse --is-inside-work-tree") !== "true") {
  process.stdout.write(
    "Integridade estado ↔ git: PROBLEMAS\n" +
      "- Diretório não é um repositório git (ou git indisponível); auditoria impossível.\n"
  );
  process.exit(1);
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
  if (!row) {
    problems.push(
      `F${fase.n}: checklist 100% fechado, mas não há linha F${fase.n} na tabela "Status das fases" do plano-firmware.md.`
    );
  } else if (!/conclu/i.test(row.status)) {
    problems.push(
      `F${fase.n}: checklist 100% fechado, mas tabela diz "${row.status}" — atualizar plano-firmware.md.`
    );
  }
}

// Invariante 3 (regra de ouro): commit que marca [x] — em contagem LÍQUIDA,
// para não acusar reescrita de linha já marcada — deve incluir outro arquivo
// de trabalho no mesmo commit (código, doc entregue ou tooling). A violação
// real é o commit que só mexe no checklist. Exceção: o commit de
// encerramento da fase, que toca o plano.
for (const fase of fases) {
  const rel = `docs/professor-virtual/fases/${fase.file}`;
  const hashes = sh(`git log --format=%H -n ${MAX_COMMITS} -- "${rel}"`)
    .split("\n")
    .filter(Boolean);
  for (const hash of hashes) {
    const diff = sh(`git show --format= --unified=0 ${hash} -- "${rel}"`);
    const added = (diff.match(/^\+- \[[xX]\]/gm) || []).length;
    const removed = (diff.match(/^-- \[[xX]\]/gm) || []).length;
    if (added - removed <= 0) continue;
    const files = sh(`git show --format= --name-only ${hash}`)
      .split("\n")
      .filter(Boolean);
    const touchesWork = files.some(
      (f) =>
        !f.startsWith("docs/professor-virtual/fases/") &&
        f !== "docs/professor-virtual/plano-firmware.md"
    );
    const isClosure = files.includes(
      "docs/professor-virtual/plano-firmware.md"
    );
    if (!touchesWork && !isClosure) {
      problems.push(
        `F${fase.n}: commit ${hash.slice(0, 7)} marca checkbox sem nenhum outro arquivo de trabalho no commit.`
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

// Invariante 4b: git diff HEAD ignora arquivos não rastreados — uma fase
// nova, ainda fora do índice, pode conter [x] sem aparecer em nenhum diff.
const untracked = sh(
  "git ls-files --others --exclude-standard -- docs/professor-virtual/fases/"
)
  .split("\n")
  .filter((f) => /fase-\d+\.md$/.test(f));
for (const f of untracked) {
  try {
    const { done } = checkboxes(readFileSync(join(root, f), "utf8"));
    if (done > 0) {
      problems.push(
        `${f}: ${done} checkbox(es) marcado(s) em arquivo não rastreado — commitar junto com o código da task.`
      );
    }
  } catch {
    problems.push(`${f}: arquivo não rastreado ilegível.`);
  }
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

- [x] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: 11 testes PASSAM (9 do validador + 2 da Task 2). Rodar também
`node .claude/hooks/pv-state-validate.mjs` na raiz do repo real e conferir
saída `OK` (ou investigar problemas reais apontados).

- [x] **Passo 5: Commit**

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

- [x] **Passo 1: Escrever o teste (deve falhar)**

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

- [x] **Passo 2: Rodar e confirmar a falha**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: o teste novo FALHA (o hook ainda não chama o validador).

- [x] **Passo 3: Implementar a chamada no hook**

Em `.claude/hooks/pv-session-context.mjs`, trocar o import:

```javascript
import { execSync } from "node:child_process";
```

por:

```javascript
import { execSync, spawnSync } from "node:child_process";
```

e acrescentar (para resolver o caminho do validador sem percent-encoding):

```javascript
import { fileURLToPath } from "node:url";
```

e inserir, entre o bloco "3. Git em uma linha" e o bloco
"4. Ponteiros permanentes":

```javascript
// 3b. Integridade estado ↔ git: só aparece quando há problemas. O caminho
// do validador é resolvido relativo a ESTE arquivo (import.meta.url), não a
// CLAUDE_PROJECT_DIR — nos testes, o project dir é um fixture temporário
// que não contém .claude/hooks/.
const validate = spawnSync(
  process.execPath,
  [
    fileURLToPath(new URL("./pv-state-validate.mjs", import.meta.url)),
    "--quiet",
  ],
  {
    cwd: root,
    encoding: "utf8",
    timeout: 10000,
    env: { ...process.env, CLAUDE_PROJECT_DIR: root },
  }
);
if (validate.status !== 0) {
  if (validate.stdout) {
    out.push(validate.stdout.trim());
  } else {
    // Timeout, módulo ausente ou crash: status ≠ 0 (ou null) sem stdout.
    // Avisar em vez de sumir com a auditoria silenciosamente.
    out.push(
      "(integridade estado ↔ git não verificada: o validador falhou ou excedeu o tempo)"
    );
  }
}
```

- [x] **Passo 4: Rodar os testes e confirmar que passam**

Rodar: `node --test ".claude/hooks/tests/*.test.mjs"`
Esperado: 12 testes PASSAM. Atenção aos dois primeiros testes da Task 2
(fixtures sem git): como o validador é resolvido via `import.meta.url`, ele
RODA também nesses fixtures e, pela invariante 0, sai com 1 — a linha
"não é um repositório git" aparece no stdout do hook, e os asserts desses
testes não conflitam com ela. Se algum deles quebrar, é regressão real.

- [x] **Passo 5: Commit**

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

- [x] **Passo 1: Acrescentar a subseção à skill**

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

- [x] **Passo 2: Verificar a coerência da skill**

Releia a skill inteira e confirme: a numeração das seções continua íntegra;
a subseção nova não contradiz "Execução" (decisões continuam passando pelo
Codex) nem "Validação" (checkbox no mesmo commit, feito pelo orquestrador).

- [x] **Passo 3: Commit**

```bash
git add .claude/skills/autonomous-phase/SKILL.md
git commit -m "Skill de fase autonoma despacha tasks pesadas a subagentes"
```

---

## Critérios de aceite do plano

- [x] `node --test ".claude/hooks/tests/*.test.mjs"` passa com 12 testes verdes.
- [x] `node .claude/hooks/pv-state-validate.mjs` na raiz do repo responde
      `Integridade estado ↔ git: OK` (ou os problemas apontados são reais e
      foram tratados).
- [x] `node .claude/hooks/pv-session-context.mjs` continua imprimindo o
      bloco de estado normal, sem erro, em repo limpo.
- [x] `TEMPLATE.md` e `SKILL.md` referenciam os mesmos literais
      (`## Contexto de retomada`, `(vazio)`).
- [x] Cinco commits pequenos, um por task; push feito somente pelo
      proprietário.

---

## Pós-revisão independente (Codex) — nota histórica

A revisão independente da execução apontou 2 findings P1 e 4 P2 (validador
fingindo OK sem git; checklist fechado sem linha na tabela; fases não
rastreadas invisíveis ao diff; falha silenciosa do validador; percent-encoding
em caminhos; retomada duplicada no stdout), corrigidos no commit `d2192fd`.

Em 28/07/2026, os snippets das tasks acima foram **retrofitados** com essas
correções (C1–C6 de `CORRECOES-E-ANALISE-planos-pre-unificacao.md`): plano e
código commitado agora coincidem — os snippets são cópia fiel dos arquivos
finais testados. Suíte final: **12 testes** (3 do hook de sessão + 9 do
validador).
