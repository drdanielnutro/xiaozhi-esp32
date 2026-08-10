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
      const m = f.match(/^fase-(\d+)([a-z]?)\.md$/);
      return m ? { file: f, n: Number(m[1]), s: m[2] } : null;
    })
    .filter(Boolean)
    .sort((a, b) => a.n - b.n || a.s.localeCompare(b.s));
}

// Rótulo humano da fase: F2, F2B, ... (sufixo do adendo em maiúscula).
function faseLabel(n, s) {
  return `F${n}${(s || "").toUpperCase()}`;
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
      const m = (cols[1] || "").match(/^F(\d+)([A-Za-z]?)/);
      return m
        ? {
            n: Number(m[1]),
            s: (m[2] || "").toLowerCase(),
            status: cols[2] || "",
            commit: cols[4] || "",
          }
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
  const rowLabel = faseLabel(row.n, row.s);
  const rowFile = `fase-${row.n}${row.s}.md`;
  const fasePath = join(fasesDir, rowFile);
  if (!existsSync(fasePath)) {
    problems.push(
      `${rowLabel}: tabela diz "${row.status}", mas fases/${rowFile} não existe.`
    );
  } else {
    const { open } = checkboxes(readFileSync(fasePath, "utf8"));
    if (open > 0) {
      problems.push(
        `${rowLabel}: tabela diz "${row.status}", mas há ${open} checkbox(es) aberto(s) em ${rowFile}.`
      );
    }
  }
  const hash = (row.commit.match(/[0-9a-f]{7,40}/i) || [])[0];
  if (!hash) {
    problems.push(`${rowLabel}: fase concluída sem hash de commit na tabela.`);
  } else if (sh(`git cat-file -t ${hash}`) !== "commit") {
    problems.push(
      `${rowLabel}: hash "${hash}" da tabela não existe neste repositório.`
    );
  }
}

// Invariante 2: checklist 100% fechado ⇒ tabela deve refletir a conclusão.
for (const fase of fases) {
  const { open, done } = checkboxes(
    readFileSync(join(fasesDir, fase.file), "utf8")
  );
  if (done === 0 || open > 0) continue;
  const label = faseLabel(fase.n, fase.s);
  const row = rows.find((r) => r.n === fase.n && r.s === fase.s);
  if (!row) {
    problems.push(
      `${label}: checklist 100% fechado, mas não há linha ${label} na tabela "Status das fases" do plano-firmware.md.`
    );
  } else if (!/conclu/i.test(row.status)) {
    problems.push(
      `${label}: checklist 100% fechado, mas tabela diz "${row.status}" — atualizar plano-firmware.md.`
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
        `${faseLabel(fase.n, fase.s)}: commit ${hash.slice(0, 7)} marca checkbox sem nenhum outro arquivo de trabalho no commit.`
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
  .filter((f) => /fase-\d+[a-z]?\.md$/.test(f));
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
