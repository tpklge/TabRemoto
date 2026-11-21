# Estado da viabilidade — atualização 20/11/2026

| Recurso | Implementação | Evidência atual | Limite |
|---|---|---|---|
| Base/teclado/cofre | BSP/LVGL/A164, AES-GCM/scrypt | Tab5 real e testes host; cadastros existentes preservados | Remoção de SD durante gravação não validada |
| SSH/SCP/SFTP | libssh2/OpenSSL, revisão fixada | Senha, chave Ed25519/passphrase, PTY/shell, listagem e roundtrips reais com hash | Terminal ANSI limitado; sem compatibilidade completa anunciada com editores |
| Wi-Fi | ESP-Hosted/C6 2.13.1 | Associação, DHCP, RSSI e reassociação após desconexão real do driver | Queda de RF prolongada não ensaiada |
| WireGuard Wi-Fi | esp_wireguard/lwIP com patch físico/CIDRs | Handshake, TCP echo, SSH no túnel e bloqueio após parar no Tab5 | Um peer, IPv4; DNS privado bloqueado para SSH obrigatório até resolver preso ao túnel |
| Ethernet/WG Ethernet | CDC-NCM16/CDC/iot_eth, host único | Baseus AX88179A 0B95:1790 identificado no Mac; framing testado em host | Link/DHCP/WG físicos pendentes: usuário só dispõe de conexão USB-C Device do Tab5 |
| RDP | OpenSSL 3.5.9, FreeRDP 3.25.0/WinPR completos no P4 | NLA/TLS, pin, desktop real, teclas/ponteiro/desconexão no Tab5, Server 2022 | Somente Wi-Fi direto/bitmap16; matriz e estabilidade prolongada pendentes |

Versões, memória, falhas anteriores e testes detalhados em [status.md](status.md)
e [rede_ssh.md](rede_ssh.md). O tamanho histórico era 3.678.224 bytes; a versão de uso com FreeRDP completo tem 5.316.128 bytes e está instalada, sem os ganchos de ensaio.

## Investigação inicial (histórico)

# Viabilidade — Marco 0

Fontes e código consultados em 19/11/2026. Compilar não equivale a validar rede.

| Recurso | Biblioteca/evidência | Dependências e consumo | Risco | Estado |
|---|---|---|---|---|
| Display/toque/SD | BSP Tab5 1.3.2, commit f259a6abd5808019b1903a422092a4234ca95a51 | IDF 5.4.4, LVGL 9.3.0, esp_lvgl_port 2.9.0; três framebuffers RGB565 ≈5,53 MB, dois buffers 720×100 ≈288 kB, rotação adicional do port | Médio: revisão deve vir do log real do BSP | Firmware mínimo compilado; resultado físico em status/testes |
| Teclado oficial | I²C A164, consulta de identidade 0xFF/versão 0xFE, endereço 0x6D | Bus dedicado GPIO0/1, IRQ50; sem fila de eventos nesta etapa | Médio: eventos e releases ainda pendentes | Detecção implementada; digitação não validada |
| SSH/SCP/SFTP | libssh2 é candidato: API de sockets/nonblocking, canais shell/exec/SFTP | Backend mbedTLS ou OpenSSL, algoritmo/formato de chave depende do build efetivo; consumo não medido | Médio/alto | Avaliação inicial; port ainda não iniciado |
| RDP NLA/TLS | FreeRDP 3.25.0, commit 90e7f064dc3ab8672b8ec714059d8cd51be0743f | OpenSSL obrigatório no CMake e nos includes TLS/certificado; WinPR, sockets, eventos/pthreads e codecs; RAM/flash finais desconhecidos | Muito alto | OpenSSL P4 incorporado; configure FreeRDP e codecs passam; WinPR completo bloqueado por APIs POSIX |
| Ethernet USB ECM | iot_usbh_ecm 0.4.0 | iot_usbh_cdc 3.1.1, iot_eth 1.1.0; um host compartilhado; heap e throughput não medidos | Médio: driver compila mas falta integrar VBUS, enumeração, netif e DHCP no Tab5 | Biblioteca compilada e símbolo linkado em prova P4 separada |
| WireGuard | trombik/esp_wireguard 0.9.0, commit 9217c5be0836e908005301ad2c2d42009e560c0e | lwIP/esp-netif/mbedTLS e crypto upstream; prova combinada ECM/WG 390.592 bytes; não representa custo incremental final | Alto: interface fixa e roteamento | Compila/linka; incompatibilidade com Ethernet confirmada no código |

## Bloqueios concretos e próximos experimentos

RDP: `artifacts/rdp-configure.log` registra erro `Could NOT find OpenSSL`, usando
o compilador RISC-V P4 e busca de bibliotecas do alvo. `WITH_OPENSSL=OFF` não
remove a dependência: `CMakeLists.txt:324` define REQUIRED e
`libfreerdp/crypto/tls.h`, `crypto.c`, `certificate.c` incluem OpenSSL diretamente.
O modo mbedTLS experimental do WinPR não é substituto completo do TLS do FreeRDP.
Próximo experimento: avaliar componente OpenSSL compatível com IDF/P4 ou port
delimitado da camada crypto/WinPR. Não usar bibliotecas macOS, desativar NLA/TLS
nem implementar protocolo/criptografia própria. A prova exigida com desktop,
tecla e clique no Tab5 continua aberta; sem servidor RDP configurado no projeto.

WireGuard: `src/wireguardif.c:914` usa literalmente `WIFI_STA_DEF` e vincula o
UDP à netif encontrada. `esp_wireguard.c:169` deixa `bind_netif=NULL`, o wrapper
expõe um único par allowed_ip/mask e `esp_wireguard_set_default()` muda a rota
default. É necessário injetar a netif física, conservar rota do endpoint,
modelar múltiplos CIDRs, restaurar DNS/rotas e provar bloqueio de fallback.
O aviso upstream em x25519 (`-Wstringop-overread`) é mantido no relatório,
não foi encoberto por alteração de criptografia. Não chamar este port de seguro
ou funcional em Ethernet apenas porque o ELF foi produzido.

ECM: a documentação oficial lista ensaios P4 com CH397A/RTL8152B/NX7202D.
Isso não comprova o adaptador do usuário. Identificar descritores/classe/VID-PID
no host do Tab5 antes de prometer compatibilidade. Um Realtek visto no Mac não
comprova que esteja conectado à USB-A do Tab5.

## Fontes primárias

- [Tab5](https://docs.m5stack.com/en/core/Tab5)
- [Teclado A164](https://docs.m5stack.com/en/tab5/Tab5_Keyboard)
- [Demo oficial](https://github.com/m5stack/M5Tab5-UserDemo)
- [BSP fixado](https://github.com/espressif/esp-bsp/tree/f259a6abd5808019b1903a422092a4234ca95a51/bsp/m5stack_tab5)
- [FreeRDP fixado](https://github.com/FreeRDP/FreeRDP/tree/90e7f064dc3ab8672b8ec714059d8cd51be0743f)
- [Compilação FreeRDP](https://github.com/FreeRDP/FreeRDP/wiki/Compilation)
- [Driver ECM](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_ecm.html)
- [Registry ECM 0.4.0](https://components.espressif.com/components/espressif/iot_usbh_ecm/versions/0.4.0)
- [USB Host P4](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- [WireGuard fixado](https://github.com/trombik/esp_wireguard/tree/9217c5be0836e908005301ad2c2d42009e560c0e)
- [Base wireguard-lwip](https://github.com/smartalock/wireguard-lwip)
- [libssh2](https://libssh2.org/)

Os clones originais são reproduzidos por `tools/fetch_research.sh`; o port
RDP usa agora `tools/prepare_rdp_sources.py`, revisões e patches registrados.
OpenSSL é incorporado ao firmware; FreeRDP/WinPR ainda não são. Locks separados preservam os componentes
do aplicativo e da prova de protocolos.

## Atualização da etapa 2 — port real em 20/11/2026

O bloqueio inicial de OpenSSL foi superado com OpenSSL 3.5.9 estático para
RISC-V/IDF 5.4.4. Sem bibliotecas macOS, gateway ou crypto própria. Build
serializado sem threads/dso/asm; não está pronto para uso concorrente geral.
Entropia pelo getrandom do SDK, com fonte SAR do P4 habilitada durante o probe.
Glue netdb aceita nomes numéricos; lookup de nomes de serviços e DNS reverso
não são suportados. Nenhuma verificação de certificado é desativada.

`tools/probe_rdp.sh` configura FreeRDP 3.25.0 contra esse OpenSSL. Alvos
freerdp-primitives e freerdp-codecs compilam. WinPR completo falha em timers
(sigaction/SA_RESTART e scheduling pthread), dlfcn.h e sys/statvfs.h.
Evidências: rdp-configure-idf.log, rdp-codecs-build.log, rdp-build-idf.log.
Não foram adicionados stubs que retornam sucesso para APIs ausentes. Próximo
trabalho é portar eventos/timers/file/library para capacidades reais do IDF,
inicializar WinPR e só depois negociar RDP/CredSSP com servidor autorizado.

O probe embarcado testa OpenSSL SHA-256, RAND, handshake TLS 1.2 e payload
criptografado por BIO pair, certificado efêmero confiado apenas nesse teste,
rejeição de hostname errado e de certificado não confiado. É um ensaio local,
sem socket, rede, CredSSP, senha ou desktop. A etapa 2 completa permanece aberta.
