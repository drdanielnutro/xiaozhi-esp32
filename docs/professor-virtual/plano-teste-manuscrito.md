# Teste do manuscrito — decide manter ou trocar a câmera CSI

**Data:** 2026-08-15 · **Natureza:** teste de bancada, sem desenvolvimento de
firmware (a placa entra apenas como câmera que gera e exporta um JPEG).

## Por que este teste

A premissa que motivou a F2B — "a CSI 1280×960 causa extração incompleta
silenciosa" — nunca foi verificada com o backend real nesta câmera. Em
2026-08-15 a extração real (`gemini.extract_lesson`, mesmo prompt e modelo de
produção) rodou sobre uma foto da placa e devolveu a página **completa e
correta**, com `illegible_pages: []` (evidência em
`evidencias/f2b/extracao-foto05-*`).

Mas aquela página é **texto impresso**. O caso que decide o produto é a
**letra da criança**, e quem a lê não é o extrator: é o **tutor**
(`gemini.evaluate_turn`), que recebe o enunciado em TEXTO (vindo da extração)
e a foto como "input da criança", precisando localizar e ler apenas a
resposta manuscrita numa página cheia de texto impresso.

Este teste exercita essa cadeia inteira, com o código real, antes de investir
nas fases F3+.

## O que o tutor recebe (fatos do código)

`evaluate_turn(input_bytes, input_mime, input_type, task_enunciado,
item_enunciado, wrong_answer_count, api_key, history_block, model)`.

NÃO recebe estado de sessão nem a lição completa — só a tarefa atual. O
prompt contém a regra "NUNCA corrija tarefas que não sejam a tarefa atual", e
o veredito admite **`unidentifiable`**, que é o sinal explícito de "não
consegui ler".

## Protocolo

1. **Página.** Usar a MESMA página já extraída (119), para reaproveitar os
   enunciados da extração limpa. Se for outra página: fotografar primeiro
   LIMPA (para a extração) e depois COM as respostas (para a tutoria) — é a
   ordem de produção.
2. **Respostas, rodada A — erradas e específicas.** Anotar ANTES o que foi
   escrito. Ex.: numerar as seis frases do exercício 3 numa ordem errada
   conhecida; marcar a alternativa **a)** no exercício 4 (a correta é **c**,
   caça às baleias). Respostas erradas específicas são o que permite provar
   leitura: um "correct" pode ser sorte, mas citar o que a criança escreveu,
   não.
3. **Respostas, rodada B — corretas.** Mesma página ou equivalente.
4. **Captura.** Mesma luz e distância das fotos anteriores; preview
   estabilizado alguns segundos antes de disparar; exportar pelo diagnóstico.
   A captura serial roda no Mac a 921600.
5. **Execução.** Para cada foto: `evaluate_turn` com `wrong_answer_count=0`
   e, em seguida, com `2` (que força a explicação completa e costuma revelar
   melhor se houve leitura real).

## Critério de aceitação

**PASSA** se, cumulativamente:

- nenhum veredito for `unidentifiable`;
- os vereditos **se diferenciarem** entre a rodada errada e a correta;
- `texto_explicacao` **citar o conteúdo específico** que a criança escreveu
  (o número que ela pôs em determinada frase, a alternativa que marcou).

**FALHA** se qualquer um ocorrer: `unidentifiable`; explicação genérica que
serviria para qualquer resposta; mesmo veredito para resposta certa e errada
(sinal de que o modelo está inferindo pelo enunciado, não lendo a folha).

## Decorrências

- **PASSA** → a OV5647 1280×960 sustenta o produto; a F3 começa com premissa
  validada e a busca por câmera de maior resolução perde urgência (vira
  melhoria, não bloqueio).
- **FALHA** → temos a prova objetiva que faltava, e a decisão passa a ser
  sobre a próxima câmera CSI, com um critério mensurável para aceitá-la.
- **PASSA COM RESSALVA** (lê, mas erra detalhes) → testar antes o ajuste de
  foco da lente (o módulo é foco fixo ~1 m e a página está a ~30 cm), que é o
  ganho mais barato disponível.

## Evidências a preservar

Fotos, JSON da extração e JSON de cada tutoria em
`docs/professor-virtual/evidencias/f2b/`, com a anotação do que foi escrito à
mão em cada rodada.
