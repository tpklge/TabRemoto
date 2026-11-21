# Ethernet USB — matriz inicial

| Driver / classe | Chip / modelo físico | VID:PID no Tab5 | Versão | Direto/hub | Estado |
|---|---|---|---|---|---|
| iot_usbh_ecm / CDC-ECM | Nenhum adaptador ensaiado no Tab5 | Não identificado | 0.4.0 + CDC 3.1.1 + iot_eth 1.1.0 / IDF 5.4.4 | Não ensaiado | Integrado; USB Host inicializado no Tab5; adaptador Ethernet físico pendente |
| CDC-NCM / RNDIS / vendor-specific | RTL8153, AX88772, AX88179 e outros | Não identificado | Sem driver escolhido | Não ensaiado | Investigação pendente; sem suporte anunciado |

No Mac foi observado Realtek `0BDA:8153` no hub do computador. Isso não identifica
um periférico da USB-A do Tab5 nem comprova CDC-ECM. Não há resultado DHCP,
throughput, hot-plug, alimentação ou coexistência HID/Ethernet nesta entrega.

O driver ECM oficial anuncia suporte P4 e documenta ensaios de certos modelos.
A compatibilidade local exige ler descritores completos e realizar link/DHCP/IP.
Integração usa um único gerenciador USB Host do BSP, compartilhado pelos clientes.
Teclado oficial I²C não deve disputar o host com Ethernet.

Na rodada de 20/11, o callback USB real detectou `0781:5567` (armazenamento
SanDisk), sem link/DHCP Ethernet. Isso não valida um adaptador de rede.

## Baseus identificado no Mac (20/11/2026)

Baseus ASIX AX88179A, VID:PID `0B95:1790`, interface `en9`, configuração USB
ativa 2: controle CDC-NCM (classe 02/subclasse 0D, interface 0) e dados
(classe 0A, interface 1). Link sem cabo no momento da leitura. Evidência:
`artifacts/baseus-usb-identification.json` e IORegistry. A Ethernet do hub
Anker é outro dispositivo: Realtek `0BDA:8153`, `en8`.

Driver NCM16 experimental derivado do ECM Espressif 0.4.0 e framing EspUsbHost
86b63f4. Seleciona configuração 2 somente para 0B95:1790, negocia NTB16/tamanho/
alinhamento, recebe blocos parciais e múltiplos datagramas com limites. Não
presume link na ausência de notificação. Teste host com sanitizers aprovado;
compilação não comprova DHCP ou transporte Ethernet. Esta revisão escolhe NCM
para o Baseus; o driver ECM anterior continua disponível como dependência,
mas não é ativado simultaneamente.

O usuário dispõe apenas da conexão USB-C. A documentação atual do Tab5 lista
USB-A como Host e USB-C como Device/carga. O caminho integrado usa o Host do
BSP na USB-A; não habilita Host/VBUS na USB-C nem valida OTG nessa porta.
Ensaio físico de link, DHCP/IP fixo, hotplug e WireGuard/Ethernet permanece
pendente de conexão física Host adequada.
Fonte: https://docs.m5stack.com/en/core/Tab5

O esquema oficial também foi baixado e lido nesta rodada (Tab5 Schematics,
J8 USB_C_16P_Horizontal): CC1/CC2 têm resistores R52/R53 de 5,1 kΩ para GND,
caracterizando a porta como sink. USB_HOST_DP/DM e USB_DVICE_DP/USB_DEVICE_DM
são redes distintas. Portanto este firmware não promete converter essa USB-C
em Host alimentado apenas por selecionar uma opção OTG. Não foi feita alteração
elétrica nem tentativa de fornecer VBUS por essa porta.
