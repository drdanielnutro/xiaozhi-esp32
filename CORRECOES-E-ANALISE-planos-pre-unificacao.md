# Correções e análise dos planos — preparação para a unificação

> **Finalidade:** antes de unificar os três documentos de replicação (proxy →
> estado por task → aprimoramentos) em um guia único, este documento (a)
> especifica as correções que faltam no `PLANO-aprimoramentos-sistema-de-fases.md`,
> (b) analisa se o `PLANO-sistema-de-fases-e-estado.md` e o
> `GUIA-REPLICACAO-proxy-autonomo.md` precisam de correções, e (c) registra as
> implicações para o guia unificado.
>
> **Método:** os findings do Codex não estão descritos nos planos — a evidência
> está **nos commits e no código final**. Parte A foi reconstruída do commit
> `d2192fd` ("Corrige findings da revisão independente (2 P1, 4 P2)") e dos
> arquivos finais. Parte B foi reconstruída da **mensagem do commit `96d2128`**
> — que lista os 6 findings daquela rodada: "persistência antes do bloqueio,
> stdin null, exit truncando stdout, assinatura por conteúdo, semântica da
> coluna Commit, try/catch total" — cruzada com a comparação entre a
> especificação do plano e o código entregue. Parte C usa a errata do documento
> original do proxy e a seção 8 do guia de replicação.
>
> Este documento foi verificado por revisão independente do Codex (2 P1 e
> 3 P2 encontrados e incorporados nesta versão).

---

## Sumário executivo

| Documento | Precisa de correção? | Quantas |
|---|---|---|
| `PLANO-aprimoramentos-sistema-de-fases.md` | **Sim** | 6 (C1–C6) + contagens de testes |
| `PLANO-sistema-de-fases-e-estado.md` | **Sim** | 6 (B1–B6), nível de especificação |
| `GUIA-REPLICACAO-proxy-autonomo.md` | **Não** | 0 (ver justificativa na Parte C) |

---

## Parte A — Correções ao `PLANO-aprimoramentos-sistema-de-fases.md`

Os snippets das tasks ainda contêm os seis defeitos que a revisão independente
encontrou. O apêndice "Pós-revisão independente" os lista, mas quem executar o
plano em outro repositório seguirá os snippets — e os reintroduzirá. As
correções abaixo trazem os snippets ao estado do código final (commit
`d2192fd`, suíte com 12 testes verdes).

### C1 (P1) — Validador finge `OK` sem git · Task 3, Passo 3

`sh()` devolve `""` tanto para "comando sem saída" quanto para "git
falhou/ausente"; fora de um repositório git, nenhuma invariante acumula
problema e o validador imprime `OK`. Inserir no snippet do
`pv-state-validate.mjs`, logo após a definição de `parseStatusTable()` e
**antes** de `const fases = listFases();`:

```javascript
// Invariante 0: sem git não há auditoria — reportar em vez de fingir OK
// (sh() devolve "" tanto para "sem histórico" quanto para "git falhou").
if (sh("git rev-parse --is-inside-work-tree") !== "true") {
  process.stdout.write(
    "Integridade estado ↔ git: PROBLEMAS\n" +
      "- Diretório não é um repositório git (ou git indisponível); auditoria impossível.\n"
  );
  process.exit(1);
}
```

E acrescentar ao arquivo de testes da Task 3:

```javascript
test("acusa ausência de git em vez de fingir OK", () => {
  const dir = mkdtempSync(join(tmpdir(), "pv-validate-"));
  mkdirSync(join(dir, "docs", "professor-virtual", "fases"), {
    recursive: true,
  });
  const r = runValidator(dir);
  assert.equal(r.status, 1);
  assert.match(r.stdout, /não é um repositório git/);
});
```

### C2 (P1) — Checklist fechado sem linha na tabela passa batido · Task 3, Passo 3

A invariante 2 do snippet só compara quando a linha da fase **existe** na
tabela (`if (row && ...)`); um checklist 100% fechado de uma fase que nunca
entrou na tabela não é acusado. Substituir, no snippet do validador:

```javascript
  const row = rows.find((r) => r.n === fase.n);
  if (row && !/conclu/i.test(row.status)) {
    problems.push(
      `F${fase.n}: checklist 100% fechado, mas tabela diz "${row.status}" — atualizar plano-firmware.md.`
    );
  }
```

por:

```javascript
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
```

Acrescentar o teste:

```javascript
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
```

**Efeito colateral obrigatório:** o fixture do teste "não acusa reformulação
de linha já marcada (líquido zero)" precisa ganhar uma task aberta
(`- [ ] T2 — leitor` nas duas escritas do arquivo), senão o checklist fica
100% fechado sem tabela e a C2 o acusa — o teste passaria a falhar por status
1. O código final dos testes já contém esse ajuste.

### C3 (P2) — Checkbox em fase não rastreada é invisível · Task 3, Passo 3

`git diff HEAD` ignora arquivos não rastreados: uma `fase-N.md` recém-criada,
fora do índice, pode conter `[x]` sem aparecer em diff algum. Inserir no
snippet do validador, logo após a invariante 4:

```javascript
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
```

Acrescentar o teste:

```javascript
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
```

### C4 (P2) — Falha silenciosa do validador some com a auditoria · Task 4, Passo 3

`if (validate.status !== 0 && validate.stdout)` descarta exatamente os casos
de timeout, módulo ausente ou crash (status ≠ 0 ou `null`, stdout vazio).
Substituir, no snippet do `pv-session-context.mjs`:

```javascript
if (validate.status !== 0 && validate.stdout) {
  out.push(validate.stdout.trim());
}
```

por:

```javascript
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

### C5 (P2) — `URL(...).pathname` quebra com espaço no caminho · Tasks 2, 3 e 4

`new URL(...).pathname` devolve caminho percent-encoded (um diretório com
espaço vira `%20` e o spawn falha). Trocar **todas** as ocorrências por
`fileURLToPath`, com o import correspondente:

```javascript
import { fileURLToPath } from "node:url";

// nos testes:
const HOOK = fileURLToPath(new URL("../pv-session-context.mjs", import.meta.url));
const VALIDATOR = fileURLToPath(new URL("../pv-state-validate.mjs", import.meta.url));

// no hook (Task 4, chamada do validador):
fileURLToPath(new URL("./pv-state-validate.mjs", import.meta.url))
```

### C6 (P2) — Retomada duplicada/instruções vazando no stdout · Task 2, Passo 3

No snippet original, a seção de retomada aparece duas vezes (dentro do dump
das 60 primeiras linhas do checklist **e** no bloco dedicado) e, quando vazia,
o dump ainda exibe título, blockquote de instrução e `(vazio)`. A implementação
deve **excisar** a seção do texto antes do dump.

**Atenção à forma da edição:** o Passo 3 da Task 2 do plano não contém um
bloco `if (faseFile) { ... }` contínuo — ele prescreve duas edições
fragmentadas, **(a)** e **(b)**. Não há alvo para substituição por busca
exata. A correção é **substituir o Passo 3 da Task 2 por inteiro** (as
edições (a) e (b) e o texto que as introduz) pela seguinte instrução única:
"Em `.claude/hooks/pv-session-context.mjs`, substituir o bloco
`if (faseFile) { ... }` (comentário '2. Checklist da fase corrente') por:"
seguida deste bloco:

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

E reforçar o teste "não imprime a seção quando está vazia" com:

```javascript
  // A seção é excisada do dump do checklist: nem o título, nem a instrução,
  // nem "(vazio)" devem aparecer no stdout quando está vazia.
  assert.doesNotMatch(r.stdout, /Contexto de retomada/);
  assert.doesNotMatch(r.stdout, /Preencha ao interromper/);
```

### Ajustes de contagem e limpeza

Com C1–C6 aplicadas ao plano:

- Task 3, Passo 2: "os 6 testes novos FALHAM" → **9 testes novos**;
  Passo 4: "8 testes PASSAM (6 do validador + 2 da Task 2)" → **11 testes
  (9 do validador + 2 da Task 2)**.
- Task 4, Passo 4: "9 testes PASSAM" → **12 testes PASSAM**.
- Critérios de aceite: "9 testes verdes" → **12 testes verdes**.
- O apêndice "Pós-revisão independente (Codex)" deixa de descrever pendências
  e pode ser reduzido a nota histórica ("findings incorporados aos snippets
  em <data>"), removendo a ressalva "o código commitado é a fonte de verdade"
  — após o retrofit, plano e código coincidem.

---

## Parte B — Análise do `PLANO-sistema-de-fases-e-estado.md`

As duas rodadas de revisão do Codex ocorreram **antes** do commit único
`96d2128`; os 6 findings estão listados na **mensagem desse commit** (citada
no cabeçalho deste documento) e materializados no código. Comparando a
especificação do plano com o código final commitado, são **seis divergências
substantivas** — cinco nos hooks (B1–B5) e uma na skill (B6); quem regenerar
os artefatos a partir do plano perde as seis:

### B1 — Assinatura de deduplicação do hook de Stop (a mais grave)

- **Plano (§3, item 3):** "Assinatura = hash de (porcelain + HEAD)".
- **Código final:** `sha256(HEAD + porcelain + diff completo dos rastreados +
  tamanho/mtime de cada entrada `??` do porcelain)` — nota: uma entrada `??`
  pode ser um **diretório** não rastreado inteiro, não necessariamente cada
  arquivo. O comentário junto à implementação explica o porquê: assinatura por
  lista de arquivos não muda quando se conclui uma task **editando os mesmos
  arquivos** — o lembrete legítimo seguinte seria suprimido pela deduplicação.
- **Correção sugerida ao plano:** reescrever o item 3 como "Assinatura
  sensível a conteúdo: hash de (HEAD + porcelain + `git diff HEAD` completo +
  tamanho/mtime de cada entrada `??` do porcelain)".
- **Observação lateral (código, não plano):** o comentário de cabeçalho de
  `pv-state-reminder.mjs` (linha 4) ainda diz "assinatura (HEAD + porcelain)"
  — está desatualizado em relação à própria implementação; corrigir quando
  houver commit naquele arquivo.

### B2 — Persistência atômica e tolerante a falha

- **Plano:** "grava a assinatura" (sem mais detalhes).
- **Código final:** a assinatura é persistida **antes** de emitir o bloqueio;
  escrita em `.tmp` + `renameSync` (atômica); se a persistência falhar,
  **sai silencioso sem bloquear** — sem deduplicação garantida, é preferível
  não lembrar a bloquear repetidamente pelo mesmo estado.
- **Correção sugerida:** acrescentar essas três decisões ao item 4 do §3.

### B3 — Entrada robusta no hook de Stop

- **Plano (§3, item 1):** "Lê o JSON do evento no stdin".
- **Código final:** stdin ilegível/JSON inválido → sai 0 sem bloquear; JSON
  **válido porém não-objeto** (ex.: `"null"`) → idem. Um hook de Stop que
  quebra com entrada malformada bloquearia toda parada de sessão.
- **Correção sugerida:** acrescentar as duas guardas ao item 1.

### B4 — Robustez e truncamento do hook de SessionStart

- **Plano (§2):** "de forma curta" e "robusto a ausência de arquivos".
- **Código final:** corte do checklist em `MAX_FASE_LINES = 60` linhas com
  ponteiro para o arquivo completo; `try/catch` global que degrada para
  "(estado parcialmente indisponível: …)" em vez de abortar; fallback
  "sem commits" no `git log`.
- **Correção sugerida:** substituir as menções genéricas por esses três
  comportamentos concretos.

### B5 — Término do processo sem truncar o stdout (finding "exit truncando stdout")

- **Plano (§3, item 4):** "emite `{"decision":"block", ...}`" — sem nada sobre
  como terminar o processo.
- **Código final:** escreve o JSON com `process.stdout.write()` e deixa o
  processo **terminar naturalmente** — não há `process.exit(0)` após a
  escrita. Chamar `process.exit()` imediatamente após um `write` pode
  encerrar antes do flush do stdout, truncando o JSON e neutralizando o
  bloqueio silenciosamente.
- **Correção sugerida:** acrescentar ao item 4 do §3: "após emitir o JSON,
  terminar naturalmente (nunca `process.exit()` logo após o write — risco de
  truncar o stdout)".

### B6 — Semântica da coluna "Commit" da tabela (finding "semântica da coluna Commit")

- **Plano (§1):** "ao encerrar a fase, atualizar a tabela macro do
  `plano-firmware.md` no commit final" — sem definir **qual hash** entra na
  coluna Commit.
- **Entregue (SKILL.md, seção Encerramento):** a coluna recebe o hash do
  **último commit de implementação da fase** — anterior ao commit de
  encerramento, porque um commit não pode conter o próprio hash.
- **Correção sugerida:** acrescentar essa definição à regra do §1.

**Veredicto:** o plano precisa das correções B1–B6 **se** for mantido como
roteiro replicável. Alternativa aceitável: marcá-lo explicitamente como
registro histórico e apontar os arquivos commitados como única fonte (mesma
solução do guia do proxy).

---

## Parte C — Análise do `GUIA-REPLICACAO-proxy-autonomo.md`

**Veredicto: não precisa de correção.** Justificativa:

1. Os defeitos da era do proxy (schema com `uniqueItems`/`minItems` rejeitado
   pela API com `400 invalid_json_schema`; truncamento de erro pela cabeça
   `slice(0, 1200)` em vez da cauda `slice(-1200)`) foram encontrados **antes
   de o guia existir** — estavam no documento original
   (`proxy-autonomo-decisoes-claude-code-codex.md`). O guia nasceu como
   consolidação **pós-correção**: a seção 8 registra o histórico, e o
   documento original carrega errata proeminente proibindo seu uso como fonte
   de código.
2. O guia replica por **cópia dos arquivos testados** da fonte da verdade, não
   por regeneração a partir de prosa — é estruturalmente imune ao tipo de
   drift que afetou os outros dois planos. (Verificado em 28/07: os arquivos
   de **código** do proxy no xiaozhi — hook, `decision-schema.json`,
   `rules/autonomy.md`, skill `architecture-mode`, `.mcp.json` — são idênticos
   aos do jarvis. `decision-policy.md` diverge por **personalização** de
   projeto; a skill `autonomous-phase` diverge sobretudo por **evolução** — o
   xiaozhi incorporou nela o estado por task, a retomada e os subagentes das
   camadas 2 e 3. O caminho da fonte no guia já foi atualizado para o macOS.)

**Ressalva para a unificação (não é defeito do guia):** o guia copia a skill
`autonomous-phase` do jarvis — a versão **base**, sem estado por task nem
subagentes. A versão do xiaozhi é a evoluída (camadas 2 e 3). O guia unificado
deve adotar a skill do xiaozhi, parametrizada, como fonte.

---

## Nota final — implicações para o guia unificado

1. **Ordem de instalação:** proxy (camada 1) → estado por task (camada 2) →
   aprimoramentos (camada 3). Nos arquivos finais do xiaozhi, as camadas 2 e 3
   **já estão fundidas** — hooks, testes, template e skill atuais contêm tudo.
2. **Estratégia:** copiar os arquivos finais do xiaozhi (e do jarvis, para o
   proxy), nunca regenerar de prosa — três episódios (proxy, estado,
   aprimoramentos) mostraram que planos em prosa/código embutido carregam
   defeitos que só a revisão independente pega.
3. **Parametrização por projeto** (mínimo): caminho `docs/professor-virtual/`,
   nome do plano mestre (`plano-firmware.md`), colunas da tabela (ex.:
   "Pendências físicas" é específica de hardware), proibições da skill (ex.:
   "não gravar em hardware"), bloco de referências do hook de SessionStart, e
   a personalização obrigatória de `AGENTS.md` + `decision-policy.md` que o
   guia do proxy já exige.
4. Os três documentos atuais permanecem como histórico e referência conceitual
   após a unificação.
