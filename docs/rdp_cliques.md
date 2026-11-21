# Correção do clique RDP — 21/11/2026

## Comportamento

Deslizar move o cursor. Toque e solte sem deslizar para clicar na posição do
cursor; pequenas oscilações do dedo ficam dentro de uma margem de 24 pixels e
não deslocam o ponteiro. Um toque parado não perde o clique por durar mais.
Para arrastar, segure por pelo menos 500 ms e faça um movimento que ultrapasse
essa margem. Um movimento iniciado antes da espera continua sendo somente
movimento, sem virar arraste posteriormente.

A seta na barra esquerda faz um clique esquerdo na posição atual do cursor.
O ícone de menu, logo abaixo, envia clique direito. Ambos preservam o foco da
entrada remota. O desktop mantém proporção, barra e renderização RGB565.

## Falhas corrigidas

- A imagem LVGL herdava a flag de rolagem, embora servisse de touchpad. Agora
  não é rolável e mantém a captura até soltar o dedo, inclusive ao sair da área.
- O reconhecimento antigo descartava movimentos acima de oito pixels e toques
  acima de 350 ms. A primeira revisão ampliou a tolerância, mas o teste completo
  revelou que um pequeno tremor após 500 ms ainda iniciava arraste. O ensaio
  nominal de 480 ms levou 750 ms no aparelho e deslocou o cursor em 7 × 5 pixels.
  Arraste agora exige o mesmo movimento intencional de 24 pixels; duração sozinha
  não transforma tremor em arraste nem cancela um clique parado.
- Pressionar e soltar um clique eram duas inserções independentes na fila,
  permitindo aceitar só uma metade quando a fila enchesse. `rdp_click` enfileira
  uma única operação; o worker envia os dois eventos consecutivamente. Arraste
  continua tendo pressionamento e soltura separados porque mantém o botão preso.

## Validação

`tests/touchpad_test.cpp` passou com ASAN/UBSAN: movimento, limites, arraste,
cancelamento, tremor, toque prolongado e toque com oscilações após mais de 500 ms.
Os 24 testes Python também passaram, incluindo dois casos do coletor USB.

`tools/check_rdp_click.py` cria somente janelas temporárias no Windows autorizado,
usando o RDP nativo do Tab5. As amostras entram num dispositivo de ponteiro LVGL,
com transformação de orientação e hit testing reais. O resultado só é positivo
quando o Windows devolve `DialogResult.OK` pelo clipboard do mesmo RDP.

Na candidata revisada passaram cinco ativações reais: toque comum; toque com
oscilações de dedo sem desvio do cursor; toque parado de 700 ms; botão lateral de
clique esquerdo; e toque sobre a própria representação do cursor. A margem de
arraste também tem teste de regressão. Evidência: `rdp-click-dialog-device.json`.
Os contadores separaram quatro cliques pelo gesto e um pelo botão lateral, sem
nenhum clique recusado pela fila.

As tentativas anteriores ficaram em artifacts/: houve erro inicial de orientação
no injetor, tentativa de abrir uma segunda sessão sem memória e falhas do método
de abrir uma janela longa de PowerShell. O teste final usa comandos curtos e
caixas modais temporárias; não depende de cliente RDP no Mac nem de gateway.

O status USB contém `rdp_touch` e contadores de clique enviados, sem conteúdo da
tela. A injeção de ponteiro é restrita à compilação de diagnóstico; a versão de
uso mantém somente as métricas de leitura. Gestos injetados validam a integração;
a confirmação com o dedo no sensor é registrada separadamente.

## Entrega

A versão de uso está instalada sem diagnósticos. Passou Wi-Fi automático, SSH,
NLA/TLS usando o pin salvo, recusa de certificado divergente e 120 segundos de
estabilidade. O backup integral conferiu o binário instalado e as imagens dos
outros aplicativos; os arquivos do cofre pessoal permaneceram idênticos.
Relatórios: `rdp-click-production-device.json` e
`rdp-click-final-preservation.json`. Recortes atuais da barra e da caixa Windows
foram lidos por USB e revisados privadamente. O coletor passou a exigir dados
numa leitura de captura e ACK simples na confirmação de snapshot.

Na janela de observacao automatizada anterior nao houve toque fisico novo.
O usuario confirmou posteriormente o funcionamento no aparelho, antes da
reformulacao das telas iniciais e de cadastro. Os contadores de leitura
continuam disponiveis sem habilitar injecoes de teste.
