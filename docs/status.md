# Interface inicial e cadastros refinados — 21/11/2026

Versao de uso instalada pelo Launcher e conferida por leitura integral:
**5.645.328 bytes**, SHA-256 `a6f6c4d3d75dec360a237558d008088b1e70e2bf1a57774239b5cd8f380bcb48`.
Diagnosticos desativados, incluindo as novas previas e capturas cifradas.
O cofre pessoal permaneceu bloqueado e byte a byte identico; imagens dos
outros aplicativos comparadas com os backups verificados.

- Inicio com quatro cartoes, icones, descricoes, favoritos e faixa de estado.
- Cadastros com menu lateral no estilo do Launcher, lista e formulario separados,
  registro selecionado destacado, legendas alinhadas e acoes consistentes.
- Opcoes abrem listas com selecao/cancelamento; protocolo RDP ajusta a porta.
- Campo mestre continua mascarado, com oito caracteres alfanumericos. Os
  formatos e a criptografia dos cadastros foram preservados.
- Tab5: seis paginas, foco, mascaras, modal, 14 capturas sem reiniciar e revisao
  visual. Producao: Wi-Fi automatico, SSH/RDP reais, pin salvo, pin divergente
  rejeitado, 30 s estaveis e retorno a tela inicial. Fontes conferidas apos
  recarregar os recursos do SD. 27 testes Python passaram.

Detalhes, limites da previa e evidencias em
[interface_launcher.md](interface_launcher.md).

---

# Clique RDP corrigido — 21/11/2026

Versão de uso instalada pelo Launcher e conferida por backup integral:
**5.607.536 bytes**, SHA-256 `e9062d369a47cae85fe75121ae7f63606a4cdbc86f93e4ca1b9f4ec34eb4b8c3`. Diagnósticos desativados; injeções por
CLI e pelo provisionamento cifrado recusadas. Cofre pessoal byte a byte
preservado e imagens dos demais apps comparadas com os backups verificados.

- Toque parado clica ao soltar, sem o limite curto de duração que descartava
  eventos. Oscilações dentro de 24 pixels não deslocam o cursor.
- Arraste exige segurar por 500 ms e mover além dessa margem; tremor não vira
  arraste por causa do atraso de renderização. A imagem não rola e mantém captura.
- Seta na barra faz clique esquerdo. Pressionar/soltar entram juntos na fila,
  evitando aceitar somente metade de um clique. Menu abaixo mantém clique direito.
- Cinco ativações reais no Windows passaram pelo ponteiro LVGL e pelo RDP
  nativo: toque normal, tremor, toque prolongado, seta lateral e toque sobre o
  cursor. O resultado foi lido do Windows, além dos contadores locais.
- Produção: Wi-Fi automático, pin persistido, SSH, certificado divergente
  rejeitado e 120 s estáveis. 24 testes Python e Touchpad ASAN/UBSAN passaram.

O usuario confirmou o funcionamento do clique no aparelho antes desta
reformulacao da interface. Detalhes e evidências em [rdp_cliques.md](rdp_cliques.md).

---

# Wi-Fi, certificados RDP e cofre corrigidos — 21/11/2026

Imagem de uso instalada pelo Launcher e conferida por leitura integral:
**5.606.032 bytes**, SHA-256 `d971fc977bcf4580ddc8a9648c76de3efa502e3979a241bb9e8a0fe4c762cf67`. Diagnósticos desativados e hooks de
ensaio recusados no aparelho. Cofre pessoal byte a byte preservado, sempre
bloqueado; imagens dos outros aplicativos comparadas com os backups verificados.

- Wi-Fi: busca real, seleção e redes salvas. Reconexão automática a redes
  conhecidas protegidas, inclusive no boot; redes abertas exigem ação manual.
- Certificados RDP: confirmação gravada antes da senha, vinculada a host/porta,
  preservada em reconexão, reboot e atualização. Pin divergente continua bloqueado.
- RDP: desenho e apresentação RGB565, cópia das regiões alteradas e remoção da
  reordenação do cursor que invalidava o desktop. Touchpad, clipboard, ícones e
  três mudanças de resolução passaram novamente no Windows autorizado.
- Cofre: scrypt com os mesmos parâmetros/formato, alocado em páginas de 64 KiB.
  Sob falta de memória total, RDP pausa e retoma após a operação. O cofre
  descartável passou KDF/AEAD, senha errada, rotação, adulteração e recuperação
  com heap fragmentado; o cofre pessoal não foi aberto pelos ensaios.
- Produção: Wi-Fi conectou sem provisionar; RDP usou o pin salvo sem reenviá-lo,
  SSH autenticou, pin inválido foi recusado e houve 120 s sem queda/reset.

Detalhes, evidências e limites: [wifi_confianca_desempenho.md](wifi_confianca_desempenho.md).
Política de AP aberto conferida no código; associação física a um AP aberto,
corte de energia e retirada física do SD permanecem sem ensaio nesta revisão.

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
Detalhes: [rdp_interface.md](rdp_interface.md).

---

# Entrega das cinco etapas restantes — 21/11/2026

Versão de uso instalada e testada pelo Launcher, app-only: **5,568,256 bytes**,
SHA-256 `5e886506d0ac40dc79c0cb9d477e5f115dd32dcb3e5abfdf335d4a84c2a1d364`. Diagnósticos desativados; comandos e injeções de ensaio
recusados no aparelho. Cofre pessoal preservado byte a byte, sempre bloqueado;
imagens Meshtastic/Tabnamp comparadas com backups integrais verificados.

- Sessões: quatro SSH e um RDP reais simultâneos, terminais isolados, seletor
  visível, alternância Ctrl+Tab e desconexão independente. Código suporta até
  quatro SSH/dois RDP, sujeito à memória; dois RDP independentes não testados.
- RDP: clipboard Unicode nos dois sentidos, 1280→960→800→1280 durante a sessão,
  quatro quedas separadas com reconexão, pin mantido e atualização do desktop.
  Crescimento sem bloco de memória suficiente mostra aviso e preserva sessão.
- WireGuard: DNS por perfil, rotas sobrepostas com seleção explícita, SSH IPv6,
  RDP IPv4/IPv6 dentro do túnel e bloqueio de ambiguidade/fallback passaram.
- Recuperação: backup/restauração cifrados em cofre descartável no SD, senha
  errada/adulteração recusadas, persistência após reboot e três remontagens
  lógicas com recarga da fonte. Cofre pessoal nunca desbloqueado pelos ensaios.
- Estabilidade: 601 s com SSH/RDP simultâneos, carga de 3.000 linhas e
  entradas periódicas; memória mínima interna 97,895 B, PSRAM
  2,846,920 B. Observação adicional de 120 s na versão de uso.

Ethernet física continua pendente: usuário dispõe somente de USB-C Device;
o Baseus AX88179A precisa de conexão Host. Somente o RDP 192.168.88.102 foi
usado como destino Windows. IPv6 externo, retirada física do SD/corte de energia
e ensaio de 24 h não foram validados. Detalhes: [etapas_finais.md](etapas_finais.md).

---

# Atualização de usabilidade — 20/11/2026

Imagem final instalada pelo Launcher: **5.401.120 bytes**, SHA-256
`4af37c7c0c8c8dd2eb24a4a21b5058fcbfa775c85efec0e388b6111109deffb5`,
app2 com 5.439.488 bytes reservados. Diagnósticos desativados e hooks
rejeitados no aparelho; NLA/TLS, pin alterado rejeitado e desktop RDP
revalidados na versão de uso. SSH e htop por comando limitado também
revalidados sem hooks; navegação física de setas/Fn passou na candidata
correspondente antes de desativar os hooks. As candidatas de
desenvolvimento passaram: layout/foco, PEM/PPK em 15 autenticações, teclas
em PTY real e htop sem watchdog, seletor de arquivo com senha digitada,
dois túneis WireGuard simultâneos (TCP/SSH, queda e reconexão independente),
e RDP NLA/TLS com confirmação visível do pin e desktop renderizado.
Vínculo atômico identidade/host e rollback passaram nos testes host do cofre.
Conferência final: cofre pessoal byte a byte igual ao backup desta revisão;
Meshtastic/Tabnamp preservados; os 17 arquivos de ensaio removidos do SD.
TabRemoto deixado na tela inicial, com Wi-Fi conectado e cofre bloqueado.
Evidências finais: usability-release-rdp.json, usability-release-ssh.json e
usability-final-preservation.json.
Detalhes, evidências e limites: [correcoes_usabilidade.md](correcoes_usabilidade.md).

---

# Estado anterior à revisão de usabilidade — 20/11/2026

Versão de uso instalada e testada no Tab5: terminal SSH expandido, cores
libvterm/fonte regular, SFTP/SCP com fila/cancelamento e WireGuard Wi-Fi.
RDP nativo NLA/TLS exibiu desktop real de Windows Server 2022 e passou pin,
teclado/ponteiro e desconexão. A tela Testes e os ganchos de ensaio estão
desativados. Ethernet física continua pendente por falta de conexão Host:
o Baseus AX88179A foi identificado; a USB-C do Tab5 é Device/recarga.

Firmware atual: **0.1.0**, **5.316.128 bytes**, SHA-256
`f26bfcf528eaec4dcb21d5a9569cd0578d179d0f31e2f69ad38f8189006c1070`.
Instalação app-only em app2 (5.373.952 bytes reservados) via Launcher;
as expansões anteriores tiveram backup integral verificado no SD e no Mac.
Cofre byte a byte igual ao backup anterior à revisão da interface;
descritores Meshtastic/Tabnamp preservados. Evidências finais:
`five-stages-production.json`, `five-stages-production-ssh.json`,
`five-stages-preservation.json`. Backups e capturas pessoais ficam fora do
pacote público. RDP permanece limitado a Wi-Fi direto/bitmap16; observação
curta não substitui estabilidade prolongada nem matriz de outros servidores.

| Área | Estado atual e evidência |
|---|---|
| Display/toque | ST7123, 1280×720; captura e resposta visual confirmadas pelo usuário |
| Teclado A164 | Modo Normal real, eventos press/release, fila limitada, composição Latin-1 em UTF-8, fila limitada e dois campos em RAM |
| Edição | Setas, Delete, Backspace, Enter, Esc, Ctrl+setas para início/fim; mapa testado em host |
| Repetição | 450 ms inicial / 55 ms; release/reset cancelam; usuário confirmou repetição e parada |
| Ensaio anterior de edição | 42 press / 42 release, zero erros I²C; artifacts/editing-samples.json. Composição/foco atuais testados automaticamente no LVGL real |
| microSD/recursos | Worker de I/O, detecção de falha/remoção, retry assíncrono e recarga de tema/locale/fonte externos; três remontagens reais aprovadas |
| Cofre/cadastros/confiança | Cadastros, favoritos, identidades e cofre AES-GCM/scrypt; teste SD, reboot e verificação independente passaram; detalhes em cadastros_seguranca.md |
| SSH/SCP/SFTP | Senha e chave Ed25519 protegida por passphrase validadas no Tab5; listagem SFTP e roundtrip SFTP/SCP com SHA-256 passaram |
| Rede do aplicativo | Wi-Fi próprio via C6 2.13.1, associação e DHCP reais; sem atualização do C6 |
| Ethernet ECM/NCM | Baseus AX88179A identificado no Mac; driver NCM16 compilado/framing testado em host; link/DHCP/hotplug físicos pendentes por conexão |
| WireGuard | Wi-Fi validado no Tab5: handshake, echo TCP, SSH com chave dentro do túnel e bloqueio após parar; Ethernet não validada |
| RDP | FreeRDP/WinPR/OpenSSL nativos; NLA/TLS, pin, desktop Server 2022, eventos de teclado/ponteiro e desconexão validados no Tab5 |

Hardware confirmado: P4 revisão 1.3, flash 16 MiB, PSRAM 32 MiB,
display/toque ST7123 firmware 3, A164 firmware 1, CPU padrão 360 MHz.

## Entrada anterior (histórico; campos Testes removidos da versão de uso)

Dois campos de diagnóstico (80 e 256 caracteres) em RAM, sem gravação do texto.
Composição de acentos e navegação Tab/Shift+Tab entre dois campos e botões
implementadas. Ainda faltam layouts configuráveis, seleção e envio para sessões. Aa funciona como Shift mantido.
Códigos Ctrl são produzidos pelo mapa, mas não executados pelo campo local.

Em falha I²C, o código limpa modificadores/fila e tenta recuperar o modo/FIFO.
O usuário relatou retorno da digitação após reconectar; a telemetria não registrou
falha I²C nessa ocasião. Portanto o ramo de recuperação de erro não é considerado
fisicamente validado. Ausência do teclado desde o boot ainda exige reiniciar.

## Evidências e histórico

- artifacts/build-editing.log e keymap-host-test.txt: build P4 e testes host de mapa, controles e temporização.
- artifacts/editing-boot.json e editing-samples.json: ensaio do firmware atual.
- Usuário confirmou cursor, Delete e repetição, inclusive parada ao soltar.
- artifacts/hardware-check.json: base inicial, 20 probes, heap por 60 s, dois ciclos Launcher/app, hash adulterado rejeitado e restauração.
- artifacts/hardware-check-first.json: falha inicial ao aguardar rede após reboot; relato inicial de botão sem resposta visível e tela azul/apagada preservado.
- Luz de fundo passou a ser controlada no boot/reboot; contador/cor do botão foram confirmados pelo usuário depois da correção.
- artifacts/hardware-check-second.json: erro da ferramenta numa escrita redundante de preferência do Launcher; limpeza verificou o valor original e apenas o boot final foi retomado. Não foi uma rodada contínua sem erros.
- artifacts/launcher-allocation.json e launcher-update.json: alocação, backup, instalação e preservação dos demais apps.

Próximos trabalhos: completar a base do Marco 1 (foco, UTF-8, recursos externos
e retry do SD) e manter a redução de risco RDP ativa. Servidores de laboratório
e adaptador Ethernet conectado ao Tab5 ainda não foram configurados/identificados.

## Etapa 1 implementada — base do aparelho

Detalhes em base_aparelho.md. Tema, locale e fonte latina são lidos do SD,
validados e usados pela UI. Fonte permanece em RAM, inclusive após remoção.
Campos preservados durante remontagem; UI não executa I/O no callback de retry.
Composição/edição UTF-8 e foco passaram em teste injetado no LVGL do Tab5;
três desmontagens/montagens reais pelo BSP e os probes passaram. Heap inicial
480.323 bytes e final 479.591 bytes, sem retenção progressiva. Oito testes de
manifest no host e os testes C++ de composição/teclado passaram.

Os dois ciclos de Launcher/app e a rejeição do manifest adulterado passaram.
Durante a restauração HTTP, o Launcher perdeu comunicação com o C6 (estado
FAILED, firmware remoto indisponível). O manifest foi restaurado por disco
USB com identidade/disco conferidos, escrita/flush/readback e ejeção antes de
retornar ao Launcher. Boot final pela USB confirmou SD gravável, recursos e
fonte externa válidos. Nenhum firmware do C6 foi alterado. O teste HTTP
interrompido continua como falho; não foi convertido em rodada sem erros.

Evidências: base-check.json, base-integration-summary.json,
usb-manifest-recovery.json, base-usb-final.json e hardware-check.json, todos
em artifacts/. O primeiro teste de heap reprovou a referência inicial de
recursos retida; ela foi removida e o ensaio repetido.

Conferência física de acentos/foco e remoção do SD solicitada ao usuário; sem
confirmação até este registro. O monitor de dois minutos não observou remoção.
Cartão somente leitura e remoção durante gravação não foram validados.

## Correção dos acentos e avanço da etapa 2

Aspas simples agora compõem acentos sem Alt: aspas + vogal, aspas + c/C
para ç/Ç. Espaço ou repetição imprime aspas literais; hint mostra o acento.
Testes C++ de eventos Normal passaram; confirmação física atual segue pendente.

OpenSSL 3.5.9 real compilado e linkado no P4; SHA-256, RAND, handshake TLS 1.2
com payload e rejeições de hostname/certificado passaram no aparelho.
A primeira rodada levou 1.704 ms e heap interno durante o worker foi de
440.607 para 197.767 bytes. A inicialização de providers fica retida;
a repetição é medida separadamente. Não se trata de conexão a servidor RDP.

A instalação maior exigiu backup completo verificado no SD e no Mac antes
da reorganização pelo Launcher. A ferramenta tentou replanejar cedo demais
enquanto o instalador automático finalizava e reportou slot inválido transitório.
A inspeção posterior confirmou TabRemoto válido e os dois demais apps
preservados; boot e ensaio TLS passaram. A ferramenta agora aguarda slots
transitórios e respostas HTTP 409 de operação em andamento. Essa falha inicial
não é registrada como execução contínua aprovada.

FreeRDP configura e seus alvos codecs/primitivas compilam; WinPR ainda falha
em timers/scheduling POSIX, dlfcn.h e statvfs.h. A etapa 2 solicitada permanece
parcial: NLA, rede, desktop, tecla e clique em servidor real não implementados.
Evidências: rdp-install-final.json, rdp-crypto-probe.json, rdp-crypto-check.json,
rdp-configure-idf.log, rdp-codecs-build.log e rdp-build-idf.log em artifacts/.

As três repetições seguintes passaram em 599/601/602 ms; heap interno final
durante o worker: 197.307 / 197.091 / 197.039 bytes, sem retenção progressiva
significativa. Após saída do worker: 212.375 bytes internos livres e
27.382.528 bytes PSRAM livres. Regressão injetada de acentos/edição/foco passou,
fonte/recursos externos válidos e zero erros I²C. Resultado: rdp-crypto-check.json.

Regressão posterior ao TLS: três remontagens do SD e testes de recursos/fonte,
edição/foco passaram. Heap inicial 212591 e final
228803 bytes. Nove testes Python e o conjunto C++ passaram.

## Etapa 3 — implementação e validação

Cadastros/favoritos/identidades/segredos, cofre AES-GCM/scrypt com senha alfanumérica
de 8 caracteres, bloqueio, troca de senha e trust explícito por endpoint.
Exportação sem segredos, importação sem sobrescrever IDs e backup criptografado
separado. Detalhes e limites: cadastros_seguranca.md.

Os testes host do núcleo passaram com AddressSanitizer/UndefinedBehaviorSanitizer,
incluindo senha errada, adulteração, falha de escrita, referências, recuperação,
export/import, backup e troca de senha. Nove testes Python e testes C++ do mapa
(incluindo todas as vogais com trema, maiúsculas e literais) passaram.

O compilador apontou um limite incorreto no novo menu. A sequência foi
interrompida antes do boot dessa imagem; a instalação do Launcher concluiu,
foi inspecionada e a imagem substituída pela corrigida antes dos testes.
As novas fontes da UI agora compilam com -Werror. Outros apps preservados.

A rodada completa do núcleo no aparelho passou: AEAD, senha incorreta,
alteração de senha, referências, confiança, adulteração e recuperação.
Persistência de favorito/identidade/chave de teste confirmada após reboot real.
O Mac validou de forma independente scrypt e AES-GCM do arquivo gerado pelo
Tab5, incluindo vínculos e favorito. security-check.json = passed.

KDF final: scrypt OpenSSL N=16.384, r=8, p=1 (~16 MiB), AES-256-GCM mbedTLS.
Preparação de cadastro: 3.166 ms; desbloqueio após reboot: 2.856 ms; conjunto
criptográfico completo: 30.354 ms. Heap após teste: 314.315 bytes internos e
27.516.352 bytes PSRAM livres. Nenhum cofre pessoal foi criado/modificado.

A verificação independente inicial falhou no coletor porque o Python do IDF
não expõe hashlib.scrypt. O coletor passou a usar cryptography.Scrypt; o
checkpoint anterior foi validado e removido, e a rodada inteira repetida
passou. Registros iniciais permanecem em artifacts/security-*-first*.json.

SSH/RDP ainda não autenticam servidores. Chaves/CA são guardadas como conteúdo
protegido; formatos, algoritmos e passphrases ainda dependem dos backends.
Teste físico dos formulários, remoção/troca de SD com cofre aberto e corte
de energia durante gravação permanecem separados dos autotestes aprovados.

A revisão final da UI restringe Tab/Shift+Tab ao cofre e às confirmações:
controles ao fundo ficam ocultos enquanto o painel/modal estiver aberto.
Autoteste no LVGL do Tab5 verificou os dois sentidos, retorno de foco,
cancelamento de modal, máscaras e todas as páginas, sem escrita no cofre.
Resultado input_selftest=2 em security-focus-check.json. A indicação de
criptografia refere-se ao cofre, não ao cartão inteiro.

Revisão final no aparelho aprovada: input_selftest=2 (trema, campos, foco e
modais), três remontagens reais do SD e três handshakes TLS locais.
Estado final em security-final-status.json; resumo em etapa3-summary.json.
Cofre pessoal inexistente/bloqueado, nenhuma senha pessoal definida. Tela
de criação do cofre aberta. Apps anteriores preservados. Heap final:
242367 bytes internos, 27313672 bytes PSRAM livres.

## Etapas 4 e 5 — integração e ensaios de rede

Wi-Fi conectado no próprio aplicativo via C6 2.13.1, sem reprogramar o módulo.
A primeira imagem falhou antes de app_main por RAM interna insuficiente no
construtor ESP-Hosted; pools e tarefas do Hosted passaram a preferir PSRAM.
O TabRemoto voltou a iniciar com aproximadamente 351 KiB internos livres.
Falha e captura preservadas em stage45-first-panic.log.

A associação inicial falhou com motivo 201. A varredura real encontrou 16–17
redes e revelou diferença entre espaços e sublinhados no SSID informado.
Com o nome transmitido correto, associação/DHCP funcionaram (canal 5).
Senhas foram transmitidas por envelope AES-GCM USB de uso único, apenas em RAM.

Servidor SSH real, porta 22: chave ED25519 confirmada explicitamente pelo
usuário antes de qualquer autenticação. Senha, PTY/shell, listagem SFTP e
roundtrip SFTP/SCP passaram no Tab5, comparando SHA-256 e tamanho dos arquivos.
Somente arquivos de teste exclusivos foram criados e removidos. Nenhuma
configuração ou authorized_keys do servidor foi alterada.
Evidência: stage45-ssh-files.json. O primeiro envio USB longo foi truncado
antes do worker SSH; cliente passou a enviar blocos com pacing, e a repetição
passou. O ensaio interrompido permanece em stage45-ssh-files-first.json.

Chave privada Ed25519 OpenSSH cifrada por passphrase autenticou no Tab5 contra
servidor SSH próprio de laboratório; marcador remoto conhecido foi recebido.
Evidência: stage45-key.json. Isso não prova todos os formatos/algoritmos de chave.

Os dois arquivos do cofre pessoal foram copiados cifrados para backup privado,
e comparados byte a byte após atualizações: permaneceram inalterados.
Não conhecemos nem redefinimos a senha mestra. Não confundimos contadores zero
com cofre vazio quando bloqueado.

Testes host: cofre com sanitizers, política de VPN e exportação sem segredos;
terminal UTF-8 parcial/cursor/OSC/limites; nove testes Python. Todos passaram.
Regressão LVGL anterior à última imagem: input_selftest=2, sem erro I²C.
USB Host inicializou e detectou armazenamento SanDisk 0781:5567; não há
adaptador Ethernet identificado para validar DHCP, throughput ou hot-plug.

Detalhes de uso e limites: [rede_ssh.md](rede_ssh.md). WireGuard em Wi-Fi foi validado conforme os resultados abaixo. Ethernet
e WireGuard sobre Ethernet seguem sem validação física.

Primeiro ensaio WireGuard interrompido por panic antes de confirmar handshake;
Launcher retornou com reset=PANIC. O build ainda tinha core locking lwIP
inativo, tornando inefetivas as seções LOCK_TCPIP_CORE usadas pelos workers.
Configuração corrigida com core locking, verificações de thread e stack TCP/IP
8 KiB. A causa exata do primeiro panic não teve backtrace capturado; essa
falha não é considerada aprovação. Evidência: stage45-wireguard-first.json.

A captura USB contínua identificou o panic: Load access fault no callback
esp_netif_internal_dhcpc_cb, chamado por netif_add. O SDK 5.4.4 estava usando
netif->state como esp_netif_t, mas o WireGuard precisa desse campo para seu
próprio contexto. CONFIG_ESP_NETIF_BRIDGE_EN habilita o caminho suportado de
metadados em client_data, separado do contexto WireGuard; nenhuma bridge é
criada. Decodificação com ELF correspondente em stage45-vpn-panic-decoded.json.
Apenas habilitar o lock não resolveu o panic; os ensaios primeiro/segundo/
terceiro permanecem falhos. A imagem corrigida passou na validação descrita abaixo.

Validação WireGuard aprovada na imagem 237afebdca75: handshake real, payload
TCP com ida/volta, autenticação SSH Ed25519/passphrase dentro do túnel e
bloqueio ao parar. O destino protegido também foi recusado em tentativa
sem VPN obrigatória, evitando fallback físico. Relógio acertado pelo Mac via
USB; SNTP não foi usado como evidência de relógio nesse ensaio. Peer Go real
userspace no Mac, sem TUN/rotas do host e sem gateway de conversão.
Evidência: stage45-wireguard.json. UDP observado no probe: 364 bytes recebidos,
770 enviados; não são métricas de throughput de aplicação. Split tunnel IPv4
sobre Wi-Fi é o cenário validado; full tunnel/IPv6/DNS/queda de Ethernet não
foram validados e não há anúncio de firewall completo.

Testes negativos SSH no servidor próprio passaram: pin alterado ficou em
Verify até cancelamento; passphrase incorreta foi recusada. Evidência:
stage45-ssh-negative.json. Nenhum teste negativo alterou o servidor do usuário.

Na imagem 237afebdca75, nova rodada SSH/SFTP/SCP passou no servidor do usuário,
incluindo os dois roundtrips com SHA-256. Desconexão Wi-Fi disparada pelo driver
foi observada e recuperou IP em 2.760 ms; a sessão SSH anterior foi encerrada,
sem reconexão silenciosa. Heap interno passou de 303.775 para 317.419 bytes e
PSRAM de 26.952.288 para 27.036.464 bytes após liberar a sessão. Isso valida
reassociação real após desconexão do driver, não queda de RF prolongada.
Evidência: stage45-wifi-reconnect.json.

Imagem final a138e59e241f instalada e iniciada: autotestes LVGL de trema,
foco e quatro páginas remotas passaram (input_selftest=2). WireGuard repetido
na imagem final passou com todos os checks, incluindo SSH com chave dentro
do túnel e bloqueio após parar. A página VPN exibe handshake, bytes e falhas;
a lista do cofre atualiza após salvar e não mostra contadores zero quando
bloqueado. Ambas as gerações pessoais seguem byte a byte idênticas ao backup
anterior às etapas 4/5: stage45-vault-preserved.json.

Finalização: SSH/SFTP/SCP e WireGuard repetidos na imagem final a138e59e241f,
com aprovação. Reinício limpo confirmou retorno SW_RESET ao Launcher e novo
boot do mesmo app. Perfis/sessões temporários foram limpos da RAM; Wi-Fi foi
reconectado e tela do cofre aberta, com cofre existente ainda bloqueado.
Servidor próprio de laboratório encerrado e configurações privadas temporárias
removidas do Mac. Confirmação visual da lista pelo usuário segue separada dos
autotestes. Ethernet continua pendente por falta de adaptador compatível.

Estabilidade final: 20 amostras por 60403 ms, sem reboot inesperado; heap interno inicial/final 330859/330859 bytes, Wi-Fi conectado. Cofre pessoal permaneceu bloqueado nesse monitor. Resultado agregado: artifacts/stage45-summary.json.

## Revisão da interface e do fluxo SSH

Relato do usuário: terminal vazio e navegação inadequada. A telemetria inicial
mostrou Wi-Fi ativo, mas SSH Idle/revision=0 e cofre bloqueado. Isso não prova
uma falha do protocolo; o fluxo anterior não tornava visíveis os erros do
cadastro, exigia procurar a confirmação de chave e não transferia o foco ao
terminal ao autenticar.

Interface reestruturada com paleta grafite/vermelho, menu lateral e cartões
inspirados no Launcher, sem copiar sua implementação. Diagnósticos foram
separados do início; listas de cadastros ganharam linhas selecionáveis.
Formulário SSH inclui porta, destino e usuário reais do cadastro, confirmação
automaticamente apresentada para chave desconhecida, erro do cofre/identidade
na própria tela e foco automático no terminal após autenticar. Campo vazio
exibe estado/instrução; fonte monoespaçada é aplicada também ao label real.

Novo tools/check_ssh_ui.py preenche widgets por configuração USB cifrada,
dispara o mesmo botão Conectar, exige Verify, clica a confirmação somente com
o pin já aprovado, aplica comando conhecido no fluxo de entrada da UI e
verifica resposta no label visível/focado. Não é teste de teclado físico.
O ensaio passou no Tab5 com servidor real. Capturas completas de início e SSH
foram recolhidas pela API do Launcher e inspecionadas no Mac; ficam privadas
em artifacts/device-private/. Ajustes finais removem rolagem da tela inteira
e destacam o foco do terminal em vermelho.

O primeiro autoteste de foco desta revisão falhou (input_selftest=3). A rodada
instrumentada distinguiu base/cofre/remoto e cinco repetições consecutivas
passaram (todos 1), em ui-refresh-focus-repeat.json. A causa da falha inicial
não foi isolada; o registro original foi preservado. A exportação de captura
pela serial sofreu timeouts; a inspeção usou download completo pelo Launcher.
Dois testes host iniciais usavam o manifest anterior ao novo tema; depois da
regeneração do manifest, os nove passaram. Núcleo do cofre e terminal passaram
com sanitizers; a mensagem de identidade ausente tem teste específico.

O ensaio de reconexão detectou um segundo problema concreto: o fingerprint já
apresentado ficava marcado entre sessões, e uma conexão nova ao mesmo host
podia ficar em Verify sem modal. O marcador de apresentação agora é limpo no
próprio evento Conectar, sem depender de observar a fase Connecting no timer.
O teste exige que o modal esteja visível antes de acionar sua confirmação.
A falha original está em ssh-ui-reconnect-first.json.

Enter nos campos do formulário SSH agora aciona a conexão. Antes, Enter era
entregue à textarea de uma linha e não iniciava sessão. Dentro do terminal,
Enter continua sendo encaminhado ao servidor. A configuração de teste cifrada
preenche o formulário e passa pelo mesmo handler de Enter.

Imagem final 958caac1af1c validada no Tab5: Enter no formulário, modal visível
antes da confirmação, autenticação, foco automático e retorno do printf no
label do terminal passaram em ssh-ui-check.json. Reconexão ao mesmo host
passou em ssh-ui-open.json, com nova confirmação visível; sessão nova foi
mantida aberta, sem marcador de teste. Cofre pessoal permaneceu cifrado e
byte a byte igual ao backup anterior à revisão. Outros aplicativos preservados.

Confirmação física do usuário na versão final: digitou “echo teste” no teclado
I²C do Tab5 e recebeu “teste” do servidor. Isso valida entrada física, envio
SSH e retorno visível nesta sessão, além do ensaio injetado. Evidência:
ui-refresh-operator.json. Sessão SSH autenticada foi deixada aberta e focada.

Monitor final: 20 amostras por 60559 ms, app pronto, SSH autenticado e terminal focado; zero erros I²C. Evidência: ui-refresh-stability.json.

## Execução das cinco próximas etapas (em andamento)

Usuário autorizou implementar todos os itens de docs/proximas_etapas.md.
Baseus identificado por IORegistry como ASIX AX88179A 0B95:1790/en9, CDC-NCM
na configuração 2; distinto do Realtek 0BDA:8153/en8 do Anker. Usuário dispõe
somente da conexão USB-C; ensaio Host/Ethernet ainda não realizado no Tab5.
Servidor RDP indicado em configuração privada de sessão. Primeira abertura TCP
pelo Mac para 192.168.88.88:3389 foi recusada, sem envio de credenciais.

Em desenvolvimento: libvterm 934bc2f com alocador PSRAM, SGR/paleta/RGB,
tela alternativa, respostas de cursor, grade 100x29 e 128 linhas de histórico;
fonte externa Roboto Mono Regular 20px (7bf0b33), fallback DejaVu para bordas.
Teste host com ASAN/UBSAN passou. Geração inicial falhou por ausência de
box-drawing na Roboto; fallback explícito corrigiu. Primeiro UTF-8 dividido
expôs troca de decoder entre chamadas libvterm; retenção de até três bytes
incompletos no wrapper passou no teste. Patches upstream limitados também
inicializam variáveis apontadas por GCC14, sem suprimir o aviso globalmente.

Primeira instalação reserva slot pelo Launcher e exige retomada: relatório
five-stages-install.log preservado. Backup integral verificado no SD e Mac;
app0/app1 mantiveram descritores válidos. Cliente interrompido após inspeção
read-only mostrar app2 reservado/vazio e operação inativa. Retomada explícita
somente após conferir SHA do binário no SD e plano sem mudança de layout.
Resultados finais da instalação/testes ainda devem ser adicionados abaixo.

WinPR completo e FreeRDP completo compilaram para P4. O port adiciona eventos
SDK eventfd, semáforos selecionáveis, timers reais esp_timer e barreira para
não liberar o argumento durante callback; remove a dependência de sigaction
para timers. Modos de arquivos POSIX indisponíveis retornam ENOTSUP/ECHILD,
sem sucesso fictício; hostname vem do esp-netif. CMake sem PIC e sem LTO:
a primeira linkedição falhou por plugin LTO, depois por POSIX opcionais e
ordem dos archives. O firmware nativo de desenvolvimento finalmente compilou
com 5.346.304 bytes (45695f4e7910), ainda sem aprovação NLA/desktop no Tab5.

RDP: o endereço .88 inicialmente informado era Linux. Usuário corrigiu para
192.168.88.102. Negociação pelo Mac confirmou NLA e TLS 1.2, sem credenciais;
certificado SHA-256 46996c79f309bd8a964f3fa9b325578bd2ac8120a0e11a9d7e568afe178ea77a,
emissor DiaDiaTech VDI RDP Issuer. Usuário reconheceu/autorizou esse pin.
Isso ainda não equivale à autenticação e ao desktop no P4.

A atualização seguinte encontrou HTTP 401 após expiração do código temporário
do Launcher. Inspeção confirmou uninstall concluído, sem repetir remoção.
Cliente agora troca o código por cookie de sessão do próprio Launcher, apenas
em RAM; permite a duração de backup e instalação. Código expirado exige
reiniciar somente o serviço HTTP pela USB. Upload da imagem usa os bytes
congelados/validados no início, mesmo se um build posterior alterar o arquivo.
Os 11 testes Python passaram, incluindo envio imutável e cabeçalhos da sessão.
Cofre host passou novamente, inclusive seleção de protocolo/domínio/pin RDP
sem cruzar a confiança SSH. Não houve reset do cofre pessoal.

A análise do esquema oficial confirma limitação da conexão solicitada para
Ethernet: J8 USB-C usa CC1/CC2 com R52/R53 de 5,1 kΩ para GND (sink), redes
USB Device separadas de USB Host. Usuário tem apenas conexão USB-C; não foi
feita alteração elétrica nem anunciado Host nessa porta. Driver NCM16 e
opções DHCP/IP fixo estão implementados/compilados, não validados fisicamente.

Primeira conexão RDP no P4 voltou ao Launcher por PANIC. Repetição com log
privado identificou assert do SDK em pthread_self: a thread RDP fora criada
com xTaskCreate. Mudou para pthread_create, com configuração/pilha explícita;
subthreads WinPR também passam pelo suporte pthread do SDK. A versão seguinte
não reiniciou, mas o autoteste de primitivas ficou preso e a conexão foi
recusada como ocupada, sem nova autenticação. Causa isolada no SDK: eventfd
vazio retorna leitura de oito bytes com zero, enquanto o laço WinPR espera
EAGAIN. Adapter do port converte a leitura vazia para esse contrato; timers
ganharam marca de cancelamento para impedir sinalização tardia. Estas correções
ainda aguardam a repetição física abaixo. Logs de falha preservados em pasta
privada; chave de provisionamento nunca entra nos logs.

Atualização: eventos/semaforos/timers passaram no aparelho (relatório
five-stages-rdp-os-eventfd.json). A interface iniciou a conexão, mas a criação
do contexto FreeRDP falhou antes de certificado ou senha. Corrigida a resolução
HOME/XDG do WinPR para /sdcard/TabRemoto e adicionada localização de erro na
construção. Nova imagem compilada 7ee8a616c613; ainda sem desktop RDP validado.
O certificado de 192.168.88.102 foi explicitamente autorizado pelo usuário.

RDP nativo validado pela primeira vez na candidata cc39a557bc5c: Tab5 autenticou
por NLA (selected_protocol=2), confirmou o SHA-256 autorizado de .102 e mostrou
desktop na interface. Dois buffers fixos PSRAM substituem cópias alocadas a cada
paint; quando ambos estão retidos, a apresentação é adiada sem perder o último
estado do decoder. Captura durante sessão, atalho Win+R, eventos de ponteiro e
desconexão passaram sem reboot. Evidências: five-stages-rdp-ui-pool.json,
five-stages-rdp-interaction.json. Validação limitada a este servidor e Wi-Fi;
não representa matriz Windows, VPN/Ethernet ou estabilidade prolongada.

SSH/SFTP/SCP, top interativo, cancelamento de uploads/fila e WireGuard Wi-Fi
revalidados na candidata; parciais de ensaio conferidos por nome exclusivo e
removidos, nenhum nome final incompleto publicado. Versão de uso f26bfcf528ea
compilada com CONFIG_TABREMOTO_DIAGNOSTICS desativado (5.316.128 bytes), incluindo
ajuste de foco RDP para não liberar modificadores repetidamente na mesma imagem.
Instalação final e teste de certificado divergente em andamento.

Entrega final f26bfcf528ea: TabRemoto aberto, Wi-Fi ativo, diagnostics=false,
recursos/fontes externos disponíveis, cofre bloqueado e zero erros I²C. Cofre e
outros apps preservados; pacote público sem backups, chaves ou capturas pessoais.
A captura final de produção foi amostrada diretamente pela USB do Launcher e
inspecionada; a rede do Launcher precisou ser dispensada nessa última coleta.
A rede Wi-Fi do TabRemoto passou normalmente. Conferência física adicional do
teclado/toque em RDP permanece aguardando resposta; eventos automatizados passaram.
