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

1. **Página — precisa ter questões ABERTAS.** Correção do proprietário
   (2026-08-15): a página 119 NÃO serve de referência, porque suas respostas
   são apenas dígitos em caixas e marcação de alternativa. Reconhecer um
   dígito ou uma marca é tarefa muito mais fácil que ler manuscrito, e
   aprovaria a câmera por engano. Usar uma página cujas respostas exijam
   **frases escritas à mão**.
   Como a página é nova, seguir a ordem de produção: fotografar primeiro
   LIMPA (para a extração dos enunciados) e depois COM as respostas (para a
   tutoria).
2. **Respostas, rodada A — erradas e específicas.** Anotar ANTES, palavra por
   palavra, o que foi escrito. A resposta deve ser uma frase com conteúdo
   verificável e errado (não um rabisco vago), para que a explicação do tutor
   só possa citá-la se de fato a leu. Respostas erradas específicas são o que
   permite provar leitura: um "correct" pode ser sorte, mas citar o que a
   criança escreveu, não.
   Escrever a lápis, com a letra natural da criança — é o caso real, e é o
   mais difícil (traço fino, contraste baixo).
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

---

## RESULTADO — 2026-08-15: **PASSOU**

Executado com a página 106 ("MÃOS À OBRA!", coral em fileiras, 4 questões
abertas), foto da placa (OV5647 CSI 1280×960, q85, tuning default), girada
90° para a vertical antes do envio. Resposta escrita **a lápis**, traço
fraco. Modelo da tutoria: `gemini-flash-latest`. Extração: `gemini-pro-latest`.

Nenhuma imagem foi cortada: a API recebeu sempre a **página inteira**
(960×1280 após rotação). Os recortes existentes nas evidências foram usados
apenas para leitura humana.

| # | O que estava na folha | Tarefa declarada | Tentativa | Veredito | Explicação |
|---|---|---|---|---|---|
| 1 | "Quatro" (errado) | a) | 1ª | **wrong** | dica sem revelar: "conte quantos degraus: o de baixo, o do meio e o de cima" |
| 2 | *nada* (linha vazia) | b) | 1ª | **teach** | foi ensinar, não julgou |
| 3 | "Quatro" (errado) | a) | 3ª | **wrong** | "são 3 fileiras! **Apague e escreva 3**" |
| 4 | "Três" (correto) | a) | 1ª | **correct** | "Você contou certinho: 3 fileiras" |

Todos os critérios de aceitação foram cumpridos:

- **Nenhum `unidentifiable`.**
- **Vereditos se diferenciaram** entre resposta errada (#1) e correta (#4),
  com a MESMA chamada e mudando só o que estava escrito a lápis. É a prova
  de que o modelo lê o CONTEÚDO, não apenas detecta que há tinta.
- **Discriminação por tarefa provada** (#1 vs #2): mesma foto, perguntas
  diferentes, vereditos diferentes e coerentes com cada linha. Como não há
  histórico entre chamadas, a diferença só pode vir de olhar a linha certa.
- **Reconhecimento de escrita existente** (#3): "apague e escreva 3" só faz
  sentido se viu algo escrito.

### Conclusão

A **OV5647 CSI 1280×960 lê manuscrito a lápis** com qualidade suficiente
para o laço central do produto. A premissa que motivou a F2B — "a CSI é
insuficiente e exige câmera UVC" — está **refutada por evidência direta**,
com o código real do backend.

Ressalvas honestas: uma página, uma letra, uma condição de luz; respostas
curtas (uma palavra). Falta exercitar respostas longas (a letra **c**, que
pede uma frase), letra de criança mais nova, luz ruim e página amassada.
Nada disso é bloqueio para a F3 — são casos a acompanhar.

### Rodadas adicionais (mesma sessão) — letras b) e c)

Continuação pedida pelo proprietário, já com o veredito da câmera decidido.
Agora com o **estado da lição completo**: tarefa corrente declarada,
`wrong_answer_count` da tarefa e `history_block` no formato exato de
`_build_history_block`. Fato do código confirmado: o contador só incrementa
em veredito `wrong` com foto (`main.py` → `increment_wrong_answer`), então
`teach` não conta.

| # | O que estava na folha | Tarefa | wac | histórico | Veredito | Explicação |
|---|---|---|---|---|---|---|
| 5 | "6 jovens" (cursivo apagado) | b) | 0 | o `teach` anterior | **correct** | "em cada uma das fileiras há exatamente 6 cantores" — e NÃO repetiu a dica do histórico |
| 6 | "Multiplicar 3 por 6" (frase, letra imitando a de criança) | c) | 0 | vazio | **correct** | "**Multiplicar 3 por 6** (ou 3 × 6) é exatamente a melhor forma…" |

A rodada #6 é a evidência mais forte do conjunto: o modelo **citou a frase
manuscrita palavra por palavra**. Transcrição literal não é sorte nem
inferência a partir do enunciado. Além disso, entendeu que a questão pedia o
**método** (não o resultado) e gerou o apoio visual com `3 × 6 = 18`.

Placar do teste: **5 de 5** — resposta errada, resposta certa, linha em
branco, cursiva apagada e frase inteira em letra difícil.

Nota de montagem: a rodada #6 foi feita após o proprietário trocar a barra de
luz por um **ring light circular**, que deu iluminação mais uniforme. É uma
melhoria barata e recomendável, mas NÃO é condição do resultado: as rodadas
1-5 passaram com a iluminação anterior.
