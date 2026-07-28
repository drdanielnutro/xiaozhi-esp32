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
  writeFileSync(fase, "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser\n");
  writeFileSync(join(dir, "modulo.c"), "int x;\n");
  g('git add -A && git commit -qm "inicial"');
  writeFileSync(
    fase,
    "# Fase F1\n\n## Tasks\n\n- [x] T1 — parser de linhas\n"
  );
  g('git add -A && git commit -qm "reformula texto de task concluida"');
  const r = runValidator(dir);
  assert.equal(r.status, 0);
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
