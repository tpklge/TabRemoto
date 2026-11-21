# Arquitetura atual

`main` inicializa BSP, armazenamento, perfis protegidos, entrada, cofre, rede,
SSH, RDP e console USB.
`tabremoto_board` usa o BSP oficial ST7123 e o teclado A164 em modo Normal.
`tabremoto_storage` centraliza `/sdcard/TabRemoto`, recursos e lock de montagem.
`tabremoto_ui` opera LVGL sob o lock do port, com overlays que restringem foco;
callbacks encaminham operações demoradas aos workers.

`tabremoto_security` serializa operações no cofre TRVAULT2 e publica somente
metadados. Novas conexões resolvem identidade/segredo/pin dentro do worker,
sem expor conteúdo privado no catálogo. `tabremoto_crypto_gate` serializa
OpenSSL e inicializações que usam a fonte de entropia SAR do P4. Alocações
OpenSSL e os pools/tarefas ESP-Hosted preferem PSRAM para preservar RAM interna.

`tabremoto_profiles` mantém redes conhecidas e pins RDP cifrados no SD, com
chave em namespace NVS próprio, cache na RAM e gravações verificadas no worker.

`tabremoto_network` inicializa esp-netif/lwIP e ESP-Hosted/C6. Wi-Fi é próprio,
sem credenciais compartilhadas em NVS. Ethernet usa USB Host do BSP, CDC/ECM
oficial e iot_eth/netif. Não anuncia suporte a RNDIS/NCM ou RTL8153 por nome.
WireGuard/lwIP recebe a interface física explicitamente. Core locking e
verificações de thread lwIP ficam habilitados. No SDK 5.4, compilar o suporte
bridge faz esp-netif guardar metadados em client_data, evitando colisão com
o contexto do WireGuard em netif->state; nenhuma bridge é criada. Um hook de roteamento
bloqueia AllowedIPs quando não há túnel utilizável; sockets SSH são presos
à interface/túnel antes do connect. Mudanças de rede invalidam sessões.

`tabremoto_ssh` usa libssh2 com OpenSSL, sockets não bloqueantes e timeout.
Chave de host é verificada antes da autenticação. Worker mantém terminal
limitado e processa filas de entrada/arquivos sem I/O na thread LVGL.
SFTP/SCP reutilizam a sessão autenticada. Arquivos abertos prendem a montagem
FAT até fechar; retry de SD não desmonta o volume entre blocos de rede.

RDP permanece indisponível: apenas a prova OpenSSL/TLS local funciona no P4.
FreeRDP configura e codecs compilam; WinPR completo ainda tem bloqueios POSIX.
Não existe gateway externo escondido nem biblioteca dinâmica carregada do SD.

O diagnóstico USB aceita configuração privada cifrada de uso único em RAM.
Logs padrão omitem terminal, listagens e credenciais. O Launcher transfere e
instala somente a imagem app-only e recursos allowlisted; backup verificado
precede mudanças de slots. Firmware C6, bootloader e eFuses não são alterados.

Bibliotecas e patches: `dependencies.lock`, `research/network-dependencies.json`,
`research/rdp-dependencies.json` e `patches/`. Limites de terminal, DNS e VPN
estão em [rede_ssh.md](rede_ssh.md); validação real em [status.md](status.md).
