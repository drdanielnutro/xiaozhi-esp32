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
});

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
