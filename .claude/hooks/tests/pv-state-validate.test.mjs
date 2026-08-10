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

test("linha F2B da tabela valida contra fase-2b.md, sem colidir com F2", () => {
  const { dir, g } = makeRepo();
  const fases = join(dir, "docs", "professor-virtual", "fases");
  // F2 em andamento com checkbox aberto; F2B concluída com checklist fechado.
  writeFileSync(
    join(fases, "fase-2.md"),
    "# Fase F2\n\n## Tasks\n\n- [ ] T6 — validação física\n"
  );
  writeFileSync(
    join(fases, "fase-2b.md"),
    "# Fase F2B\n\n## Tasks\n\n- [x] T1 — spike\n"
  );
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  const hash = g("git rev-parse --short HEAD").trim();
  writeFileSync(
    join(dir, "docs", "professor-virtual", "plano-firmware.md"),
    "# Plano\n\n## Status das fases\n\n" +
      "| Fase | Status | Concluída em | Commit | Pendências físicas |\n" +
      "|---|---|---|---|---|\n" +
      "| F2 — Câmera | em andamento | — | — | T6 |\n" +
      `| F2B — Resolução | concluída | 2026-08-10 | ${hash} | — |\n`
  );
  g('git add -A && git commit -qm "tabela"');
  const r = runValidator(dir);
  // Sem o sufixo no parser, a linha F2B seria lida como F2 e acusaria o
  // checkbox aberto de fase-2.md. Com o parser correto, tudo consistente.
  assert.equal(r.status, 0);
  assert.doesNotMatch(r.stdout, /F2B.*fase-2\.md/);
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
