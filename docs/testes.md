# Interface inicial e cadastros — 21/11/2026

`tools/check_launcher_ui.py` passou no Tab5 com seis paginas, limites dos
rotulos dos botoes, selecao de protocolo, cancelamento, foco do teclado,
mascaras e acesso ao desbloqueio. Foram 13 capturas cifradas e uma captura
normal sem reiniciar; as imagens foram revisadas privadamente.

A captura cifrada inicial reproduziu uma falha de protecao da pilha da tarefa
USB. O buffer de linha de 3.840 bytes passou para alocacao separada, com falha
explicita de alocacao. Os dois caminhos de captura passaram novamente.
O coletor tambem exige o campo correto no desafio/recibo de provisionamento e
no resultado de acao, e ACK simples na aplicacao cifrada. Tres regressoes novas
passaram, com 27 testes Python no total.

`tools/check_release.py --saved-profiles --stability-seconds 30` passou na
versao sem diagnosticos: hooks novos e antigos recusados, Wi-Fi automatico,
SSH, RDP NLA/TLS com pin salvo, pin divergente bloqueado e estabilidade.
Leitura integral verificou a imagem instalada, os demais apps e o cofre.
A aplicacao terminou aberta no inicio, com Wi-Fi automatico apos reboot;
recursos do SD recarregados e recortes atuais dos rotulos lidos por USB.
Relatorios `launcher-ui-*` em artifacts/; capturas e backups sao privados.

A previa nao desbloqueia nem grava o cofre pessoal. A apresentacao de registros
pessoais apos desbloqueio normal deve ser conferida pelo usuario; nenhum segredo
pessoal foi aberto para produzir imagens de teste.

---

# Regressão do clique RDP — 21/11/2026

`tools/check_rdp_click.py` passou na candidata revisada com cinco ativações de
botão Windows, conferindo `DialogResult.OK` pelo clipboard do RDP nativo. A
injeção percorre LVGL, hit testing e orientação reais; cobre tremor, gesto
prolongado, seta lateral e sobreposição do cursor. O ensaio inicial reproduziu
um toque tratado como arraste e deslocamento de 7 × 5 pixels; o caso tem regressão
host e passou no aparelho após corrigir o critério de início do arraste.

`tests/touchpad_test.cpp` passou com ASAN/UBSAN; 24 testes Python passaram,
incluindo proteção do coletor contra ACK de captura confundido com status atrasado.
`tools/check_release.py --saved-profiles` passou na versão sem diagnósticos:
recusa também a nova injeção de ponteiro, autentica SSH/RDP, usa pin salvo, bloqueia
pin divergente e observa 120 s de estabilidade. Leitura integral confirmou app,
cofre pessoal e demais apps. Recortes atuais da barra e da caixa Windows foram
lidos diretamente por USB após nova captura, sem usar a captura antiga da coleta
HTTP. São imagens privadas, fora do pacote público.

Funcionamento do clique confirmado pelo usuario no aparelho; relatórios `rdp-click-*` em
artifacts/. Resultados e modo de usar em [rdp_cliques.md](rdp_cliques.md).

---

# Wi-Fi, confiança persistente e memória — 21/11/2026

A imagem de uso e sua leitura de verificação estão no início de [status.md](status.md).
`tools/check_wifi_trust_perf.py` passou na candidata com busca/seleção, arquivos
cifrados, reboot real sem provisionar Wi-Fi, certificado sem repetir aprovação e
cofre descartável com retomada RDP. Para repetir com o mesmo certificado salvo,
use `--known-certificate`, sem apagar a confiança já cadastrada.

A candidata RGB565 passou `check_native_rdp` e o conjunto de sidebar/touchpad,
incluindo coordenadas reais do Windows, clipboard, resize e remontagem lógica.
A produção passou `tools/check_release.py --saved-profiles`, com hooks recusados,
Wi-Fi automático, SSH, certificado divergente bloqueado e autenticação usando
apenas o pin persistido, além de 120 segundos estáveis. Esse modo exige um boot
fresco para conferir a conexão Wi-Fi automática.

Host: 22 testes Python, regiões/cópias com ASAN/UBSAN, cofre com sanitizadores e
provider paginado, 16 derivações comparadas com scrypt sem a adaptação e quatro
falhas de alocação com limpeza integral. Relatórios começam com
`wifi-trust-perf-` em artifacts/. O backup integral confirmou o binário de uso,
os outros apps e o cofre pessoal sem alterações.

Gestos são injetados no dispatcher real; não houve atuador físico. Não havia AP
aberto de laboratório. A comparação de tempos de cópia usa amostras distintas,
sem alegação de equivalência de FPS com o cliente do Mac. Mais detalhes em
[wifi_confianca_desempenho.md](wifi_confianca_desempenho.md).

---

# Revisão da interface RDP — 21/11/2026

Build P4 e Touchpad ASAN/UBSAN passaram. 22 testes Python existentes passaram.
`tools/check_rdp_sidebar.py` passou no Tab5 com o servidor Windows autorizado:
layout real, ícones, limite de labels, no-op de resolução repetido, movimento
relativo sem clique, tap e cancelamento de arraste, Cursor.Position via clipboard,
três trocas de resolução, clipboard, menu e 60 s sem reset/erro de resize.
`tools/check_release.py --report artifacts/rdp-sidebar-production-device.json`
passou com diagnósticos off, inclusive rejeição dos novos hooks, SSH, certificado
RDP divergente rejeitado, pin aprovado e 120 s estáveis. Captura real inspecionada
privadamente; evidências e limites em [rdp_interface.md](rdp_interface.md).

---

# Testes da entrega das cinco etapas — 21/11/2026

Resultados atuais e limites em [etapas_finais.md](etapas_finais.md).
A imagem de produção é a registrada no início de [status.md](status.md).
Scripts check_sessions, check_rdp_clipboard, check_rdp_reconnect,
check_advanced_network, check_rdp_vpn, check_dns_scope e
check_remaining_stability usam build temporária com diagnósticos. Já
check_release verifica a versão sem hooks, inclusive recusa do pin divergente
antes de autenticar e recusa de transporte inválido antes de DNS/TCP.

Credenciais chegam por stdin sem eco e provisionamento AES-GCM; não usar
argumentos/logs para senhas. Apenas um coletor serial por vez. Falha de ACK
é reconciliada por recibo, sem repetição da ação. Relatórios de teste ficam
em artifacts/, backups/capturas/fixtures em diretórios privados fora do pacote.

A versão atual passou todos os ensaios possíveis descritos no relatório;
Ethernet física, outro servidor Windows, dois RDP independentes, IPv6 externo,
SD removido/corte de energia e 24 h continuam pendentes de condições externas.

---

# Testes — 19/11/2026

Relatórios são produzidos pelos scripts; resultados finais em `docs/status.md`.

## Build e host

- `bash tools/build.sh build` / `artifacts/build.log`: firmware ESP32-P4 0.1.0.
- `bash tools/build.sh size` / `artifacts/size.log`: imagem 849.568 bytes;
  seções de imagem 849.169 bytes, DIRAM estática 101.637 bytes. Os buffers de
  display são alocados no boot e não estão todos nessa conta estática.
- `python -m unittest discover -s tests -v` / `artifacts/host-tests.log`: oito
  casos, pacote válido, corrupção, schema/firmware incompatível, arquivo ausente,
  traversal, inventário privado e symlink rejeitados. Esses testes executam o
  validador do empacotador no Mac; não substituem o validador C++ no aparelho.
- `bash tools/probe_rdp.sh` / `artifacts/rdp-configure.log`: falha real de
  cross-configuração na dependência OpenSSL, sem backend RDP gerado.
- `research/protocol_probe`: bibliotecas ECM/WireGuard compiladas no GCC RISC-V
  e linkadas no ELF de prova. Símbolos `esp_wireguard_connect`, `wireguardif_init`
  e `iot_eth_new_usb_ecm` foram conferidos com `riscv32-esp-elf-nm`. A prova não
  foi instalada nem criou um túnel/link. Não representa consumo incremental do
  produto nem desempenho de rede.

## Instalação real pelo Launcher

`artifacts/launcher-before.json`, `launcher-allocation.json` e
`launcher-install.json` registram antes/depois, backup e plano.
Launcher 0.8.0-rc.2, firmware C6 2.13.1, USB Serial/JTAG na porta observada
`/dev/cu.usbmodem12201` (o número pode mudar).

A alocação criou `app2`, com 851.968 bytes. O Launcher reiniciou antes da gravação
do app; o cliente reconciliou o slot vazio e concluiu a instalação. Os slots
0/Meshtastic e 1/Tabnamp mantiveram nome/versão/descritor. Antes da mudança foi
feito backup integral de 16.777.216 bytes, baixado e validado por SHA-256.
O binário final foi enviado e relido, hash
`0dd0c1473a8be7bfd3698a4b6916cad89677e2e7c39e28d35ba296c133195d1b`.

Os relatórios distinguem a alocação da instalação: o campo
`partition_table_changed=false` da instalação final significa que ela usou o
slot já criado; a alocação anterior alterou a tabela pelo próprio Launcher.

## Ensaio automatizado no Tab5

Executar `python tools/hardware_check.py` com o Python do IDF e Launcher aberto.
O script deixa TabRemoto executando ao terminar. Relatórios:
`artifacts/hardware-check.json`, `hardware-serial.log`, `hardware-run.log` e
`tabremoto-screen.ppm`.

O ensaio verifica boot do app instalado, detecção do hardware, SD e recursos;
executa vinte probes exclusivos com write/fsync/read/compare/remove; observa
heap durante 60 segundos; captura a árvore LVGL 1280×720; retorna ao Launcher
e inicia o app novamente em dois ciclos; adultera somente o hash de um recurso
no manifest público e verifica rejeição com diagnóstico disponível; restaura
os bytes originais e confirma novo boot válido. Não injeta toques fictícios.

Toque real é observado pelo contador do botão; confirmação visual do usuário
é registrada separadamente quando fornecida. A captura prova renderização,
não brilho, cores físicas ou calibração completa. Detecção I²C não comprova
digitação ou releases. Sessão de um minuto não é um teste de longa duração.

O primeiro ensaio falhou ao aguardar HTTP do Launcher após o reboot, pois ele
estava sem IP. A preferência daquele boot não foi registrada. Também houve relato de tela
azul/apagada e botão sem resposta visível. Os relatórios `*-first.*` preservam
esse resultado. A repetição usa o controle corrigido da luz de fundo e ativa
temporariamente `wifiAuto` quando necessário, restaurando exatamente o valor
anterior antes do boot final. Na repetição a opção já estava habilitada.
A preferência original é registrada sem SSID/senhas no relatório.

Resultado final: `artifacts/hardware-check.json` registra aprovação das etapas
acima. A ferramenta encontrou um erro HTTP 400 numa gravação redundante da
preferência do Launcher ao terminar; a limpeza restaurou/verificou o valor.
A correção evita a escrita quando o valor já coincide. `--resume-final` retomou
somente a verificação do boot final, sob pré-condições dos resultados anteriores,
preservando o erro em `test_harness_issues`. Não foi uma rodada contínua sem erros.
O relato visual positivo do usuário está em `artifacts/operator-validation.json`;
o primeiro relato negativo e os ensaios interrompidos permanecem documentados.

## Pendentes

SD ausente/RO/removido durante escrita exige ensaio físico controlado; não foi
simulado como aprovação. Cadastro persistente, cofre, senhas/chaves, confiança,
terminal/teclado, SCP/SFTP, Ethernet, VPN em ambos os transportes e RDP requerem
implementação e servidores reais. Não houve scan de rede nem uso de credenciais
ou alteração de políticas em servidores do usuário.

## Etapa 1 — base, UTF-8, foco e recursos externos

Build e testes atualizados em 20/11/2026. tests/keymap_test.cpp inclui composição
Latin-1/UTF-8, combinações sem forma composta e cancelamento de acento. O teste
C++ foi executado no host com SDK macOS 15.4. Oito testes Python de manifest
passaram com a nova allowlist de cinco arquivos.

tools/check_base.py executa composição/edição UTF-8 e Tab/Shift+Tab no LVGL do
Tab5 por entrada injetada conhecida, preservando os textos anteriores. Isso
não equivale à entrada física. Três remontagens reais pelo BSP, seguidas de
probes e recarga dos recursos/fonte, passaram. Heap inicial 480.323 bytes,
final 479.591 bytes. Primeiro ensaio falhou por retenção do pacote inicial
(base-check-first.json); a referência permanente foi removida e o ensaio repetido.

O novo teste completo usa hardware-base-run.log e hardware-check.json. Remoção
física e somente leitura devem ser marcados separadamente; simulação ou remount
do cartão presente não provam esses cenários. Estado final em docs/status.md.

## Correção dos acentos / etapa 2

Testes C++ do mapa verificam aspas + a/c/Aa+C e escape literal sem Alt.
Oito testes Python de manifest, build P4 e sintaxe dos scripts passaram.
OpenSSL foi compilado para RISC-V e incorporado ao app, sem bibliotecas host.
FreeRDP configure e alvos freerdp-primitives/freerdp-codecs passaram; build
completo falhou nas APIs POSIX ausentes registradas em rdp-build-idf.log.

`python tools/check_rdp_crypto.py` mede três rodadas no aparelho, com SHA-256,
RAND, handshake TLS 1.2 com payload, rejeição de hostname e confiança inválidos,
heap antes/depois e regressão de edição/foco LVGL. O par TLS é local por BIO;
não prova rede, NLA ou servidor. Resultado em artifacts/rdp-crypto-check.json.
O primeiro coletor usou nomes de campos errados na asserção após o probe passar;
o coletor versionado usa os nomes reais e executa novos testes completos.

Regressão após TLS: três remontagens do SD e nova composição/foco passaram
(base-check.json). Nove testes Python passaram, incluindo a espera de slot
inválido transitório/HTTP 409 sem repetir operação de instalação.

## Etapa 3 — cadastros e segurança

Núcleo testado em host com AddressSanitizer e UndefinedBehaviorSanitizer:
AEAD/KDF, senha alfanumérica de oito caracteres, erro de senha, alteração do
conteúdo cifrado, referências, troca de senha, recuperação, export/import e
backup. Testes e build em tests/security_host e artifacts/security-host-*.log.

No Tab5, a primeira rodada falhou porque mkdtemp retorna ENOSYS no newlib
deste SDK (confirmado no disassembly e na captura). O auxiliar de testes passou
a usar mkdir exclusivo, sem contornar criptografia ou armazenamento. Uma
coleta seguinte foi interrompida por duas leituras concorrentes da serial; o
worker continuou no aparelho. Resultados iniciais ficam preservados; a rodada
completa exige um único cliente USB.

Regressões da versão com o cofre: três probes TLS locais passaram; três
remontagens de SD e todos os autotestes LVGL de trema/edição/foco/formulários
passaram. Novas telas compilam com -Werror. Persistência com reboot e
verificação AEAD independente no Mac ficam em security-check.json.

Resultado final do núcleo: security-check.json = passed, com reboot real e
AEAD independente no Mac. O coletor inicial usava hashlib.scrypt indisponível
no Python do IDF; foi corrigido para cryptography e a rodada completa repetida.
PBKDF2 foi avaliado e removido por custo de ~64 s; formato final TRVAULT2
usa scrypt upstream (N=16.384, r=8, p=1). Preparação: 3.166 ms, desbloqueio
pós-reboot: 2.856 ms. A adaptação experimental PKCS5 não integra o firmware.

A regressão de foco exercita todas as páginas, 40 ciclos Tab/Shift+Tab no
painel e oito trocas dentro de modal, cancelamento e retorno à tela original.
Controles de fundo não recebem foco. input_selftest=2 no Tab5. A revisão
final preserva o mesmo núcleo de cofre validado em security-check.json.

Revisão final instalada: três ciclos TLS e três remontagens do SD passaram
novamente, com regressão de trema/foco/modal. Estado: security-final-status.json
e resumo etapa3-summary.json. Não houve confirmação física de digitação nos
novos formulários nem testes de autenticação em servidores.

## Etapas 4/5 — ensaios atuais

`tools/check_ssh_files.py` recebe a configuração privada por stdin sem eco,
provisiona por USB, testa autenticação/pin, listagem e roundtrip SFTP/SCP com
hash, e remove somente arquivos exclusivos da rodada. Resultado no Tab5:
`artifacts/stage45-ssh-files.json`. O primeiro erro do transporte USB longo
fica em `stage45-ssh-files-first.json`; pacing de 48 bytes corrigiu o coletor.

`tools/check_lab_ssh.py` usa servidor próprio `research/wg_lab` e chave Ed25519
OpenSSH cifrada por passphrase. Não altera o servidor do usuário. Resultado:
`stage45-key.json`. Dados privados da fixture ficam fora da distribuição.

`tests/terminal_test.cpp` passou com AddressSanitizer/UndefinedBehaviorSanitizer:
UTF-8 dividido entre leituras, cursor/apagamento, descarte OSC e tela limitada.
O teste inicialmente tinha comprimento literal incorreto, detectado por ASAN;
o comprimento foi corrigido com strlen antes da rodada aprovada.
`tests/security_host/vault_test.cpp` inclui referências VPN, remoção protegida
e exportação que mantém VPN obrigatória. Resultado: security-host-final45.log.

`tools/check_wireguard.py` exige handshake real, echo TCP pelo túnel, SSH com
chave dentro do túnel e bloqueio ao parar. Só reporta aprovação após todos os
passos; compilação e criação de netif não contam como validação. Ethernet
exige adaptador conectado à USB-A do Tab5 e servidor disponível.

## Revisão de UI/SSH

`tools/check_ssh_ui.py` recebe credenciais por stdin sem eco e aciona widgets
reais via LVGL: formulário, botão Conectar, modal de chave e entrada no
terminal. O marcador de saída é diferente do texto digitado (printf com %s),
impedindo aceitar apenas eco de PTY como execução do comando. O label visível
e o foco são conferidos por diagnóstico sob lock LVGL. Resultado em
artifacts/ssh-ui-check.json; não substitui teclado I²C físico.

Capturas: execute snapshot no app, retorne ao Launcher e baixe somente
/TabRemoto/cache/screen.ppm pela API já autenticada. `tools/ppm_to_png.py`
converte o RGB PPM sem perda para inspeção. Não publicar capturas com dados
pessoais. A tentativa de leitura em blocos pela serial não foi confiável e
não é o transporte utilizado como evidência visual.

O usuário confirmou o teste físico na versão final: teclado oficial digitou
echo teste e o servidor respondeu teste. Registro ui-refresh-operator.json.

## Revisão das cinco etapas — testes de host

`tests/terminal_test.cpp` agora exercita libvterm com ASAN/UBSAN: UTF-8 em
leituras separadas, SGR padrão/paleta/RGB/reset, OSC descartado, tela alternativa,
resposta de cursor e 128 linhas de histórico com 100 colunas. O teste do parser
anterior não é reutilizado como prova do novo emulador.

`tests/ncm_test.cpp` validou encode/decode NTB16, divisor/resto de alinhamento,
tabela cíclica e 30 mil entradas aleatórias sob sanitizers. Não prova RX/TX USB,
notificação de link ou DHCP no Baseus físico. O driver foi compilado para P4.

Onze testes Python passaram após estender os recursos públicos para sete
arquivos: inclui corrupção/schema/path traversal/privacidade, imagem congelada
antes do upload, cabeçalhos da sessão cookie e transição do Launcher.
Cofre host passou com nova sessão RDP: domínio e senha da identidade, protocolo
do destino e fingerprint separado do SSH. Testes não abrem o cofre pessoal.

Captura privada `five-stages-terminal.png` obtida do framebuffer do Tab5 e
inspecionada: terminal quase em tela cheia, só Desconectar, vermelho/verde/azul,
truecolor e á/é/í/ó/ú/ç/ü. Comando executado por SSH real; não é mock de label.
Primeiro smoke do coletor tentou SSH antes de concluir DHCP e falhou em TCP;
a repetição com Wi-Fi estabelecido passou. A captura não substitui avaliação
física dos pixels nem ensaio de longa duração.

O coletor do Launcher passou a ter 12 testes: timeout de GET durante gravação
não repete execute; cookie não retransmite o código temporário; upload usa os
bytes congelados da imagem. Uma rodada real expirou o código de 15 minutos;
outra perdeu um GET durante erase/backup. As operações terminaram no aparelho
e foram reconciliadas por leitura antes de retomar somente a instalação.

Primitivas WinPR no P4: após corrigir a thread POSIX, o primeiro ensaio ficou
preso no reset de evento (contrato eventfd vazio do SDK). Corrigido isso, a
repetição passou eventos e falhou em semáforo inicialmente positivo. A leitura
do SDK confirmou value=initval mas is_set=false; o adapter agora cria zero e
sinaliza por write, mantendo contador/máximo próprios. Não houve sucesso
fictício nem remoção dessas asserções. A próxima repetição deve testar também
timer periódico/cancelamento/fechamento. Relatórios negativos são preservados.

Repetição física das primitivas WinPR aprovada: eventos, reset/sinalização,
semáforos inicialmente positivos, timeout, timer periódico e cancelamento.
Evidência: artifacts/five-stages-rdp-os-eventfd.json. A tentativa seguinte pela
interface falhou antes de negociar/autenticar: não criou o contexto FreeRDP
(artifacts/five-stages-rdp-ui-first.json). O port agora resolve HOME/XDG para
a raiz exclusiva no SD e registra apenas o ponto de falha de construção,
sem credenciais. Compilação 7ee8a616c613 concluída; ensaio físico em andamento.

Na imagem 7ee8a616c613, o contexto foi criado, mas o preparo dos codecs esgotou
PSRAM. A limpeza chamou aligned_free sobre um bloco já liberado pelo caminho
de falha de aligned_recalloc. O log de backtrace ainda acionou o stub abortivo
de unwind do SDK. Correções: recalloc preserva o original quando a reserva
falha; desativado unwind indisponível; perfil bitmap de 16 bits sem codecs GFX
não anunciados. Não houve autenticação NLA aprovada nesta tentativa.

RDP cc39a557bc5c aprovado no Tab5: primitivas (incluindo falha de recalloc sem
liberar o bloco original), diálogo real de certificado, autenticação NLA,
quadro recebido/renderizado/foco, captura em sessão, scancodes Win+R com novo
quadro do servidor, movimento/down/up do mouse e desconexão. Relatórios:
five-stages-rdp-os-eventfd.json, five-stages-rdp-ui-pool.json e
five-stages-rdp-interaction.json. A imagem anterior autenticava/renderizava,
mas falhou em alocação contínua durante captura; preservado relatório negativo.
A resolução negociada é 1280x720, cor remota 16 bits, superfície BGRA32 local;
GFX/RemoteFX/NSCodec desativados explicitamente. Sem gateway externo.

Regressões na candidata cc39a557bc5c: SSH pela interface/Enter/confiança/foco e
terminal expandido aprovados; top interativo, saída por q e retorno ao shell
aprovados. SFTP e SCP ida/volta com SHA-256 e limpeza de arquivos próprios
aprovados. Seleção Ethernet com Wi-Fi ativo rejeitou IP e hostname sem enviar
credenciais. WireGuard Wi-Fi repetiu handshake, TCP echo, SSH por chave no túnel
e bloqueio após parar, inclusive sem require_vpn. Relatórios five-stages-ssh-
interactive.json, five-stages-ssh-files.json, five-stages-transport-policy.json,
five-stages-wireguard.json. O fixture userspace do Mac foi encerrado e suas
chaves temporárias removidas. Primeiro preparo do teste de cancelamento usou
pasta incorreta no SD, foi recusado sem transferência; arquivo próprio movido
por upload verificado para transfers/downloads e original removido.

Cancelamento físico de upload SFTP e SCP aprovado: fixture próprio de 2 MiB,
interrupção após pelo menos 64 KiB, segundo envio enfileirado, reconexão e
listagem confirmaram ausência dos nomes finais. Caminhos parciais retornados
pelo backend foram identificados e verificados/removidos; fixture local
removido. Relatório five-stages-file-cancel.json. Nenhum arquivo do usuário
foi alvo da limpeza. O script inicial de coleta esperava um nome de parcial
sem o prefixo do destino e foi corrigido para o formato real usado pelo app.

Versão de uso f26bfcf528ea instalada e aprovada: diagnostics=false, quatro
ganchos de desenvolvimento recusados, certificado divergente bloqueado antes
de autenticar, reconexão com pin autorizado/NLA e desktop, captura ao vivo e
observação curta sem reboot. PSRAM livre mínima nessa observação: 7.795.140 B;
19 quadros e quatro liberações iniciais de modificadores, sem injeção repetida
por foco. Relatório five-stages-production.json. A observação de 25 segundos
não equivale a ensaio prolongado de estabilidade.

A primeira captura da versão de uso ocorreu na transição preta inicial da
sessão; ela não serve como prova visual de desktop completo. A coleta final
foi ajustada para capturar também após a observação, sem mudar o firmware.

A parada final RDP demorou mais que a espera fixa de 2 s do coletor; a consulta
seguinte confirmou Closed, sem erro/reboot, e devolução dos buffers (26,4 MB de
PSRAM livres). Esse timeout de asserção não foi interpretado como falha de
protocolo; futuros coletores devem aguardar a fase Closed em vez de sleep fixo.
A conferência física adicional de teclado/toque RDP foi solicitada ao usuário;
os eventos de rede automatizados passaram, mas essa confirmação ainda falta.

Preservação: store.a (1451 bytes) e store.b (1235 bytes) iguais ao baseline
vault-before-ui e ao backup anterior às cinco etapas. O baseline mais antigo
vault-before-stage45 tinha store.a anterior a novos cadastros; não foi
restaurado. Meshtastic/app0 e Tabnamp/app1 mantêm nome, versão e descritor válido.

Coleta final sem depender da rede do Launcher: após wifi-retry não manter a
associação, a API USB read forneceu amostras exatas de cada oitava linha/coluna
da captura de produção, inspecionadas visualmente com janelas reais do desktop
(14.093 de 14.400 pixels acima do limiar preto). Não é imagem fabricada nem
substitui a captura integral inicial de desenvolvimento. Relatório
five-stages-production-pixels.json. Os dois arquivos do cofre foram conferidos
novamente pela USB. Boot USB em app2 e provisionamento privado restabeleceram
Wi-Fi no aplicativo. Estado final aprovado: ready, diagnostics=false, fontes
externas/recursos ativos, cofre existente/bloqueado, zero erros I²C, 26.890.668 B
de PSRAM livres. Relatório five-stages-final-state.json.

## Correções de usabilidade — 20/11/2026

- `usability-python-tests.log`: 17 testes Python, incluindo retry limitado do
  challenge criptográfico e garantia de não repetir ação com ACK perdido.
- `usability-ppk-host.json`: 12 conversões PPK reais comparadas com PuTTYgen,
  formatos v2/v3, três algoritmos, com/sem senha, adulteração e limites.
- `usability-key-import-device.json`: 15 autenticações reais por arquivo no
  Tab5 e rejeição de passphrase/MAC errados antes de iniciar TCP.
- `usability-keyfile-ui-device.json`: seletor de arquivo, PPK cifrado,
  passphrase pelo dispatch físico A164, Enter, confiança e seleção de hosts.
- `usability-ui-multivpn-device.json`: dois arquivos .conf pela UI, dois peers
  reais simultâneos, TCP/SSH em cada um, parada/reconexão independentes,
  CIDR conflitante rejeitado e bloqueio após parar todos.
- `usability-terminal-keys-device.json`: bytes exatos de application cursor,
  F1/F10/F11/F12/Shift+Tab, htop real e retorno ao shell sem watchdog.
- `ssh-ui-check.json`: foco/edição e geometria dos campos, formulário SSH,
  confirmação visível e marcador desenhado. Captura de rede inspecionada.
- `usability-native-rdp-ui.json`: NLA/TLS, pin autorizado, confirmação visível,
  autenticação, desktop real e foco com a nova interface.
- Testes C++ com sanitizers de mapa/terminal/importação/CIDRs, e cofre host
  com vínculo atômico e rollback. Dados pessoais não usados nesses testes.

Essas evidências são de candidatas de desenvolvimento. A entrega de uso
exige separadamente imagem com diagnósticos desativados, verificação do
cofre e dos descritores dos outros apps, mais regressão SSH/RDP de produção.

Produção desta revisão: `usability-release-rdp.json` e
`usability-release-ssh.json` aprovados. Imagem app-only 4af37c7c0c8c,
5.401.120 bytes, CONFIG_TABREMOTO_DIAGNOSTICS desativado. Reconciliação da
instalação automática em `usability-release-reconciled-install.json`;
nenhuma repetição de execute após o reboot. Backup integral verificado antes
da reorganização, cofre byte a byte igual e outros apps preservados.
