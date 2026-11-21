# Atualização do clique — 21/11/2026

Toque e solte sem deslizar para clicar, com tolerância ao tremor. Segurar e mover
além da margem inicia arraste. A seta na barra envia clique esquerdo; o ícone de
menu abaixo envia clique direito. A ativação de cinco botões Windows foi
conferida pelo RDP real, incluindo toque sobre o cursor. Imagem atual e limites
em [status.md](status.md) e [rdp_cliques.md](rdp_cliques.md).

---

# Atualização de desempenho e persistência — 21/11/2026

A versão atual usa RGB565 desde o FreeRDP até o LVGL, copia regiões modificadas e
preserva a ordem das camadas do cursor. Certificados aprovados persistem fora do
cofre para não repetir a confirmação. O cofre pode pausar/retomar RDP quando
precisa de memória. Barra, touchpad, clipboard, layout e resize foram revalidados
no Tab5; detalhes e imagem atual em [status.md](status.md) e
[wifi_confianca_desempenho.md](wifi_confianca_desempenho.md).

---

# Interface RDP revisada — 21/11/2026

Imagem de uso instalada e validada no Tab5 pelo Launcher: **5574496 bytes**,
SHA-256 `933018845a48f25425c22be6c278cd505cbe2e98d3e8625310b01c0701827c42`. Diagnósticos desativados; hooks de toque e inspeção de layout
recusados no aparelho. Cofre pessoal byte a byte preservado e imagens dos outros
aplicativos conferidas por backup integral/readback. RDP deixado conectado
somente ao servidor autorizado 192.168.88.102.

- Barra esquerda exclusiva de 68 px, ícones sem texto e desktop na área restante,
  com proporção preservada. Controles não cobrem mais a imagem remota.
- Touchpad relativo com cursor visível: deslizar move sem clicar, toque curto
  clica, segurar antes de mover arrasta; cancelamento solta o botão. Ícone de
  menu envia clique direito. Jitter pequeno não invalida um toque.
- Seletor de resolução compacto e avisos temporários, preservando o desktop
  atrás. Selecionar a resolução atual é uma operação nula; não reserva memória
  nem envia pedido ao servidor. Resize de geometria idêntica não limpa pixels.
- Botões comuns têm ícones e texto centralizado/limitado à área útil; botões
  estreitos da tela inicial usam ícone acima do texto para manter legibilidade.
- Testes reais passaram: barra fora do desktop, ícones, limites dos labels,
  vinte pedidos da resolução atual sem aviso, movimento/tap/arraste pelo
  dispatcher real, Cursor.Position do Windows confirmando as coordenadas, sequência
  1280→960→800→1280, clipboard, menu e estabilidade. A versão de uso passou
  também SSH, NLA/TLS/pin e 120 s de observação.

Limites: gestos foram injetados no mesmo dispatcher da entrada LVGL no aparelho;
não há atuador pressionando fisicamente o sensor. Ampliação ainda pode ser
recusada se não houver memória para as novas superfícies; nesses casos o
motivo é específico e o desktop atual permanece aberto. Não se afirma que toda
piscada possível de rede/display foi eliminada fora dos cenários ensaiados.

## Gestos e controles

A barra mantém sessões, clipboard, resolução, menu de clique direito e
desconexão. O desktop é ajustado à área restante, sem distorcer a
proporção ou mudar automaticamente a resolução negociada.

Deslizar reposiciona o cursor em relação à posição anterior, independente de
onde o dedo toca. Tocar e soltar em qualquer ponto clica na posição atual do
cursor, com tolerância ao tremor e sem limite curto de duração. Segure por pelo
menos 500 ms e mova além da margem de 24 pixels para arrastar;
soltar ou cancelar encerra o arraste. Um swipe normal longo não vira arraste.
Duas batidas curtas produzem dois cliques; multitouch/rolagem por dois dedos
não fazem parte desta implementação.

## Resolução e avisos

A resolução atual é desabilitada no seletor e aceita como no-op no backend,
mesmo sem disp. Pedidos dentro da capacidade reservada não exigem novos buffers
de pixels. Ampliação continua fazendo reserva antes de modificar o remoto;
falta de memória não derruba a sessão. Ausência do canal, tamanho inválido,
recusa do servidor, falta de memória e timeout têm mensagens distintas.

O seletor e o aviso pertencem ao painel RDP, sem esconder a tela inteira ou
alternar para o formulário. Vinte seleções repetidas da resolução atual e três
mudanças reais não produziram avisos indevidos nos testes. Não se generaliza
esse resultado para toda falha física de display/rede.

## Falhas encontradas na validação

A carga recursiva da fonte externa sobrescrevia a fonte dos símbolos. Os
rótulos com fontes específicas agora são preservados inclusive após remontar
o SD. A checagem de rede também encontrou texto automático quebrando em duas
linhas em um botão curto; a altura agora acompanha uma linha da fonte, com
reticências se necessário. O cabeçalho Terminal SSH ganhou largura adequada.

O primeiro coletor de posição não recebeu retorno do comando Windows; a prova
foi simplificada para Cursor.Position e repetida com êxito. Um Escape do coletor
foi explicitamente recusado por fila momentaneamente ocupada; a ferramenta
passou a respeitar essa contrapressão, repetindo somente eventos recusados.
Os relatórios das tentativas ficaram em artifacts/, separados do resultado final.

## Evidências em artifacts/

- rdp-sidebar-native.json: NLA/TLS, certificado aprovado e desktop real.
- rdp-sidebar-device.json: layout, ícones, no-op, gestos, coordenadas confirmadas
  pelo Windows, resoluções, clipboard/menu e 60 s sem reset/erro de resize.
- rdp-sidebar-host-tests.json e rdp-sidebar-python-tests.log: testes host.
- rdp-sidebar-production-device.json: versão sem hooks, SSH, pin divergente,
  desktop aprovado e 120 s de estabilidade.
- rdp-sidebar-final-preservation.json: cofre, outros apps e imagem instalada
  conferidos por leitura do backup integral de 16 MiB.
- rdp-sidebar-public-package-audit.json: distribuição sem dados privados.

As capturas e os backups ficam em diretórios privados, fora da distribuição.
O Launcher precisou de um ciclo pelo app antigo para recuperar associação/IP;
a instalação só começou depois de confirmar Wi-Fi conectado. Nenhum firmware
C6 foi regravado. Os testes nunca desbloquearam ou restauraram o cofre pessoal.
