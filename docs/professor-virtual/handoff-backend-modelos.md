# Handoff ao backend (`licao_casa`) — separar os modelos Gemini

Pedido gerado em 2026-08-15 pelo firmware do Professor Virtual. O `licao_casa`
é dependência somente leitura deste repositório: a mudança é ação do
proprietário no outro projeto.

Texto abaixo, objetivo de propósito — o agente do backend precisa saber o que
fazer e o que não quebrar; a justificativa da escolha dos modelos é decisão já
tomada pelo proprietário e ficaria só como ruído.

---

## Texto do pedido

> **Objetivo:** usar modelos Gemini diferentes em cada chamada:
> `gemini-pro-latest` na extração da lição e `gemini-flash-latest` na tutoria.
>
> **O que existe hoje** (verificado no código):
>
> - `backend/config.py:21` — uma única configuração
>   `gemini_model: str = "gemini-3-flash-preview"`.
> - `backend/main.py:471` — `extract_lesson(..., model=settings.gemini_model)`
> - `backend/main.py:847` — `evaluate_turn(..., model=settings.gemini_model)`
> - `backend/gemini.py:375` e `:480` — parâmetros `model=` com defaults, mas
>   nunca usados: os dois pontos de chamada sempre passam o valor.
>
> Como a configuração é uma só, não basta trocar strings: é preciso desdobrar
> em duas e apontar cada ponto de chamada para a sua.
>
> **Restrições:**
>
> 1. Não alterar o miolo pedagógico (prompts, vereditos, escalada de dicas,
>    máquina de estados da sessão). A mudança é de configuração e dos dois
>    pontos de chamada.
> 2. Preservar compatibilidade com quem já define o modelo por variável de
>    ambiente. Sugestão: as novas configurações caem para o valor de
>    `gemini_model` quando não definidas.
> 3. Os nomes das variáveis de ambiente mudam junto com os nomes dos campos
>    (pydantic settings) — documentar os novos.
> 4. `gemini-pro-latest` e `gemini-flash-latest` foram verificados como
>    disponíveis na chave de API do projeto.
>
> **Validação:** suíte de testes existente + smoke do fluxo real (`prepare` de
> uma página seguido de um `turn` com foto).

---

## Nota interna (não faz parte do pedido)

A escolha veio da bancada de 2026-08-15: a descrição visual gerada pela
extração vira o contexto visual do tutor em todos os turnos seguintes, e um
erro ali se propaga em silêncio. Detalhes e medições em
`plano-teste-manuscrito.md` e no decision-log (`PV-ModelosGemini`).
