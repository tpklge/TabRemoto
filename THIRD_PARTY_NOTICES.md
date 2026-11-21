# Dependências e licenças

- ESP-IDF 5.4.4: Espressif, Apache-2.0 e componentes de terceiros com avisos
  próprios, incluindo FreeRTOS, newlib, lwIP, mbedTLS e FatFs. Consultar LICENSE
  e COPYRIGHT dos componentes no checkout do SDK.
- espressif/m5stack_tab5 1.3.2 e esp_lvgl_port 2.9.0: Apache-2.0.
- LVGL 9.3.0: MIT. Fontes Montserrat: avisos/licença presentes no componente LVGL.
- Dependências transitivas do BSP: versões e hashes integrais em
  `dependencies.lock`; licenças dos componentes gerenciados são copiadas no pacote.
  A presença no lock não significa que todo o código foi linkado ao aplicativo.
- Prova separada iot_usbh_ecm 0.4.0 / iot_usbh_cdc 3.1.1 / iot_eth 1.1.0:
  Espressif, Apache-2.0, lock em `research/protocol_probe/dependencies.lock`.
- FreeRDP 3.25.0 / WinPR: Apache-2.0, commit fixado em
  `research/rdp-dependencies.json`. Cliente nativo incorporado ao firmware TabRemoto.
- OpenSSL 3.5.9: Apache-2.0, revisão fixada no mesmo inventário; bibliotecas
  estáticas RISC-V incorporadas aos clientes SSH e RDP do firmware. Licença em
  `licenses/openssl/LICENSE.txt` no pacote.
- Pesquisa esp_wireguard 0.9.0: manifesto BSD-3-Clause; os arquivos incorporados
  do wireguard-lwip e das primitivas crypto têm avisos próprios, preservados no
  upstream. Integrado ao firmware com patch de seleção de interface física e estatísticas; sem mudanças nas primitivas criptográficas.
- libssh2: integrado ao firmware com OpenSSL, revisão fixada em
  `research/network-dependencies.json`; licença BSD em `licenses/libssh2/COPYING`.
  Patch GCC14 delimitado em `patches/libssh2-p4.patch`.

Patches de plataforma em `patches/`: OpenSSL usa select, evita colisão com
nomes lwIP e trata ausência de processos setuid no FreeRTOS; WinPR renomeia
seu closesocket para evitar colisão com lwIP. Nenhuma primitiva criptográfica
foi reimplementada. O driver A164 usa registradores e matriz oficiais;
não copia firmware STM32 ou código de outros aplicativos.

O cliente de manutenção importa o cliente Python local do LauncherdoPaludo.
O projeto usa BSP e bibliotecas upstream, não copia o código do Launcher ou do
Tab5Player para dentro do firmware.

A fonte externa latin20.bin deriva de Montserrat-Medium.ttf distribuído pelo
LVGL 9.3.0 (SHA-256 do TTF em artifacts/font-build.json), sob SIL OFL 1.1.
OFL.txt é incluído junto da fonte em /TabRemoto/assets/fonts/. Conversor de
build: lv_font_conv 1.5.3; não é dependência de execução do firmware.

A etapa 3 usa AES-GCM do mbedTLS do SDK e scrypt do OpenSSL portado.
A experiência com yield no PKCS5 foi removida da implementação final; não há
patch no mbedTLS ou no SDK local.

## PuTTY 0.83 — leitor de chaves PPK

Código de sshpubk, codecs de chave/crypto e utilitários sob a licença MIT de
PuTTY (research/putty/LICENCE). Origem e SHA-256 em putty-dependencies.json.
O adaptador usa ppk_load_s do upstream para PPK 2/3, com checagem de MAC e
passphrase, e o escritor OpenSSH upstream adaptado apenas para saída em RAM.
RSA, ECDSA NIST e Ed25519; Argon2 limitado a 16 MiB/100 passes/8 lanes no Tab5.
O patch delimita a tabela de algoritmos e acrescenta yield ao Argon2 no SDK;
não substitui primitivas criptográficas. Não há subprocesso nem PuTTYgen externo
como dependência do aplicativo no Tab5.

## c-ares 1.34.9

DNS assíncrono por perfil: [c-ares](https://c-ares.org/), licença MIT.
Arquivo e SHA-256 fixados em `research/cares-dependencies.json`.
Patch de plataforma em `patches/cares-1.34.9-esp-idf.patch`: entropia real do
ESP-IDF, serviços/portas numéricos e ausência de FD_CLOEXEC (não há exec no ESP).
Cada consulta usa canal independente, DNS explícito e socket vinculado antes
de qualquer consulta; não há cache global compartilhado entre túneis.

A adaptacao de alocacao de scrypt em `patches/openssl-3.5.9-esp-idf.patch`
divide a tabela V em paginas (64 KiB para r=8), sem alterar as rodadas, N/r/p
ou os bytes derivados. O caminho upstream contiguo permanece nos demais builds;
`TABREMOTO_SCRYPT_PAGED` habilita o mesmo caminho segmentado nos testes host.
