#!/usr/bin/env node
// Hook Stop: lembra de atualizar o estado por task quando há mudanças não
// commitadas. A verdade vem do git (não de flags de uso de ferramenta).
// Deduplicação por assinatura (HEAD + porcelain): nunca lembra duas vezes
// pelo mesmo estado; anti-loop via stop_hook_active.

import { readFileSync, writeFileSync, mkdirSync, renameSync, statSync } from "node:fs";
import { execSync } from "node:child_process";
import { createHash } from "node:crypto";
import { resolve, join } from "node:path";

const root = resolve(process.env.CLAUDE_PROJECT_DIR || process.cwd());
const stateDir = join(root, ".claude", "state");
const statePath = join(stateDir, "stop-reminder.json");

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

let event = {};
try {
  event = JSON.parse(readFileSync(0, "utf8") || "{}");
} catch {
  // entrada ilegível: não bloquear nada
  process.exit(0);
}
if (event === null || typeof event !== "object") {
  // JSON válido porém não-objeto (ex.: "null"): não bloquear nada
  process.exit(0);
}

if (event.stop_hook_active === true) {
  process.exit(0);
}

const porcelain = sh("git status --porcelain");
if (!porcelain) {
  process.exit(0);
}

const head = sh("git rev-parse HEAD");
// Assinatura sensível a CONTEÚDO, não só à lista de arquivos: diff completo
// dos rastreados + tamanho/mtime dos não rastreados (senão, concluir uma
// task editando os mesmos arquivos não geraria novo lembrete).
const trackedDiff = sh("git diff HEAD");
const untrackedStat = porcelain
  .split("\n")
  .filter((l) => l.startsWith("??"))
  .map((l) => {
    const p = l.slice(3).trim();
    try {
      const s = statSync(join(root, p));
      return `${p}:${s.size}:${s.mtimeMs}`;
    } catch {
      return `${p}:?`;
    }
  })
  .join("\n");
const signature = createHash("sha256")
  .update(head + "\n" + porcelain + "\n" + trackedDiff + "\n" + untrackedStat)
  .digest("hex");

let lastSignature = "";
try {
  lastSignature = JSON.parse(readFileSync(statePath, "utf8")).signature || "";
} catch {
  // sem estado anterior
}
if (signature === lastSignature) {
  process.exit(0);
}

try {
  mkdirSync(stateDir, { recursive: true });
  const tmpPath = statePath + ".tmp";
  writeFileSync(
    tmpPath,
    JSON.stringify({ signature, at: new Date().toISOString() }) + "\n",
    "utf8"
  );
  renameSync(tmpPath, statePath);
} catch {
  // sem persistência não há deduplicação garantida: sair silencioso é
  // preferível a bloquear repetidamente pelo mesmo estado
  process.exit(0);
}

process.stdout.write(
  JSON.stringify({
    decision: "block",
    reason:
      "Lembrete de estado (hook, uma vez por estado do worktree): há mudanças " +
      "não commitadas. Se uma task foi concluída, marque o checkbox em " +
      "docs/professor-virtual/fases/fase-N.md e commite junto com o código; " +
      "se a fase encerrou, atualize também a tabela 'Status das fases' do " +
      "plano-firmware.md. Se o trabalho está no meio (ou as mudanças são do " +
      "proprietário), apenas prossiga e encerre normalmente.",
  })
);
