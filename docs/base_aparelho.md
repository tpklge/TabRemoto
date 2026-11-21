# Base do aparelho — implementação da etapa 1

## Entrada e foco

Dois campos mantêm texto somente em RAM (80 e 256 caracteres). Tab/Shift+Tab
navegam pelos campos e botões; Enter ou Espaço ativam botão. O foco tem contorno
visível. Toque também escolhe o campo. Troca de foco/Esc limpa composição,
modificadores e repetição, preservando os textos. Backspace durante um acento
pendente cancela só o acento; em texto UTF-8 remove um caractere completo.

Mapa físico A164, Aa mantido como Shift e Sym mantido para símbolos. Para
compor: mantenha Alt e pressione o acento, solte as teclas e digite a vogal.
Disponíveis: agudo ('), grave (`), circunflexo (^), til (~) e trema (").
Alt+C produz ç; Alt+Aa+C produz Ç. Uma combinação sem forma composta insere
o acento seguido da letra; acento seguido de Espaço insere o próprio acento.
Exemplo: Alt+' e depois a produz á. Til usa Alt+Aa+` e depois a/o.

As formas geradas estão em Unicode Latin-1, armazenadas em UTF-8. Isto não é
um IME Unicode geral, layout ABNT2 completo ou suporte de entrada para todos
os idiomas. Códigos Ctrl continuam separados de texto; não são executados
neste formulário local. Seleção e clipboard ainda não foram implementados.

## Recursos externos

O manifest valida cinco arquivos públicos por allowlist e SHA-256. Tema,
mensagens e fonte Montserrat de 20 px U+0020–U+00FF são carregados de
/TabRemoto/; a licença OFL acompanha a fonte. A fonte é gerada a partir do
TTF do componente LVGL 9.3.0 fixado no lock, por tools/build_font.py e
lv_font_conv 1.5.3. Sem fonte/SD válido permanece o diagnóstico interno mínimo.

O worker lê recursos em buffers limitados; o LVGL cria a fonte usando MEMFS
sobre RAM, sem ler o cartão durante desenho. O pacote de fonte em RAM permanece
válido quando o SD é removido e é substituído/liberado ao carregar outro pacote.
Tema fornece cores e a localização fornece título, instrução e botão de retry.
Checksums verificam corrupção e não autenticam uma atualização.

## microSD e recuperação

Um worker possui montagem/remontagem e verifica o estado do cartão a cada
segundo. Ausência/falha invalida a disponibilidade de operações de disco.
O botão “Tentar SD novamente” solicita desmontagem/montagem e validação em
segundo plano. Os campos e foco não são recriados nessa operação. Não há
formatação automática nem gravação de texto dos campos.

Um mutex serializa montagem, probes e captura; outro protege apenas a publicação
do estado em RAM. O loop de UI não espera por I/O do cartão. Remontagem também
revalida o manifest e recarrega recursos. SD sem escrita não é anunciado como
persistente. Os probes usam arquivo temporário exclusivo, fsync e comparação.

A remoção física e o cartão somente leitura precisam de ensaios próprios;
remontar um cartão presente não equivale a esses testes. Ainda não existem
sessões/transferências para validar políticas de pausa durante remoção.

## Testes

- tests/keymap_test.cpp: composição, combinações inválidas, cancelamento,
  controles, releases e repetição no host.
- tools/check_base.py: fonte real do SD, composição/edição/foco no LVGL do Tab5
  com entradas injetadas explicitamente como teste, três remontagens e heap.
  Os textos existentes são restaurados; não grava o conteúdo do usuário em logs.
- tools/hardware_check.py: probes, reboots, manifest adulterado e restauração.
- Ensaios de teclado físico e remoção do cartão são registrados separadamente.

O primeiro teste de heap reprovou uma referência ao pacote inicial guardada pela
inicialização. A referência permanente foi removida; o relatório original foi
preservado em artifacts/base-check-first.json. Resultado final em docs/status.md.

Para reconstruir a fonte em outro ambiente: instale as dependências de build
com `npm ci --prefix tools/font-builder --ignore-scripts` e execute
`python tools/build_font.py --converter tools/font-builder/node_modules/lv_font_conv/lv_font_conv.js`.
Node é apenas ferramenta de build; a fonte pronta já acompanha o projeto.

## Recuperação independente da rede

O teste de manifest adulterado encontrou falha de comunicação do C6 no Launcher
antes da restauração HTTP. O original foi restaurado por disco USB: identidade
303A:4002 / Tab5 microSD, disco novo identificado no Mac, comparação com a
adulteração conhecida, escrita/flush/readback do único manifest, ejeção pelo
macOS e retorno por comando USB do Launcher. O boot posterior confirmou
recursos e fonte válidos. Evidência: artifacts/usb-manifest-recovery.json e
base-usb-final.json. O relatório HTTP interrompido permanece preservado.

Ferramenta correspondente: tools/usb_sd_recover.py (macOS, PyUSB/libusb). Ela
recusa diferenças que não sejam a adulteração conhecida do teste e nunca formata
o cartão ou grava firmware. O firmware do C6 não foi reprogramado.
