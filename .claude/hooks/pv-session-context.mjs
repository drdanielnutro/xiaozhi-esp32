#!/usr/bin/env node
// Hook SessionStart: injeta o estado do projeto Professor Virtual no
// contexto de toda sessão nova (startup/resume/clear/compact).
// Saída curta por design: tabela de fases + checklist da fase corrente + git.

import { readFileSync, readdirSync, existsSync } from "node:fs";
import { execSync } from "node:child_process";
import { resolve, join } from "node:path";

const root = resolve(process.env.CLAUDE_PROJECT_DIR || process.cwd());
const planoPath = join(root, "docs", "professor-virtual", "plano-firmware.md");
const fasesDir = join(root, "docs", "professor-virtual", "fases");
const MAX_FASE_LINES = 60;

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

const out = [];
out.push("=== Estado do projeto Professor Virtual (hook SessionStart) ===");

try {

// 1. Tabela "Status das fases" do plano
if (existsSync(planoPath)) {
  const plano = readFileSync(planoPath, "utf8");
  const section = plano.match(/## Status das fases[\s\S]*?(?=\n## |$)/);
  if (section) {
    const table = section[0]
      .split("\n")
      .filter((l) => l.startsWith("|") || l.startsWith("## "))
      .join("\n");
    out.push(table || "Tabela de status não encontrada no plano.");
  } else {
    out.push("Seção 'Status das fases' não encontrada no plano.");
  }
} else {
  out.push("plano-firmware.md ainda não existe.");
}

// 2. Checklist da fase corrente (fase-N.md de maior N)
let faseFile = null;
if (existsSync(fasesDir)) {
  const fases = readdirSync(fasesDir)
    .map((f) => {
      const m = f.match(/^fase-(\d+)\.md$/);
      return m ? { f, n: Number(m[1]) } : null;
    })
    .filter(Boolean)
    .sort((a, b) => a.n - b.n);
  if (fases.length > 0) {
    faseFile = fases[fases.length - 1].f;
  }
}
if (faseFile) {
  const faseText = readFileSync(join(fasesDir, faseFile), "utf8");
  const lines = faseText.split("\n");
  out.push(`--- Checklist da fase corrente (${faseFile}) ---`);
  out.push(lines.slice(0, MAX_FASE_LINES).join("\n"));
  if (lines.length > MAX_FASE_LINES) {
    out.push(`(... truncado; leia docs/professor-virtual/fases/${faseFile})`);
  }
  // A retomada sai sempre integral: o corte de 60 linhas acima não pode
  // engolir justamente o registro de onde o trabalho parou. A decisão usa o
  // CORPO ÚTIL da seção (sem blockquote e sem comentários HTML): o literal
  // "(vazio)" também existe dentro da instrução do template e não pode
  // contar como conteúdo.
  const retomada = faseText.match(
    /## Contexto de retomada[\s\S]*?(?=\n## |$)/
  );
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
} else {
  out.push("Nenhuma fase iniciada (sem fase-N.md em docs/professor-virtual/fases/).");
}

// 3. Git em uma linha
const lastCommit = sh("git log --oneline -1") || "sem commits";
const porcelain = sh("git status --porcelain");
const pending = porcelain ? porcelain.split("\n").length : 0;
out.push(`--- Git: ${lastCommit} | mudanças não commitadas: ${pending} ---`);

// 4. Ponteiros permanentes
out.push(
  "Referências: docs/professor-virtual/plano-firmware.md (plano e fases) · " +
    "docs/professor-virtual/placa-esp32-p4-wifi6-touch-lcd-7b.md (dossiê da placa, " +
    "inclui conexão serial COM3/interop)."
);

} catch (error) {
  out.push(`(estado parcialmente indisponível: ${String(error && error.message)})`);
}

process.stdout.write(out.join("\n") + "\n");
process.exitCode = 0;
