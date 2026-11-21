# Teclado oficial — descoberta

Tab5 Keyboard **A164** usa I²C na Ext.Port1, não USB HID físico.
SDA GPIO0, SCL GPIO1, endereço 0x6D, IRQ GPIO50.
O BSP usa o outro barramento: GPIO31/32, `CONFIG_BSP_I2C_NUM=1`.

Nesta etapa consultamos apenas endereço em 0xFF e versão em 0xFE, com timeout,
sem selecionar modo, consumir FIFO ou alterar firmware/endereço.
Detecção no aparelho não comprova digitação, modificadores ou releases.

O próximo marco deve implementar eventos reais, fila limitada, estados de
pressionar/soltar, Ctrl/Alt/Shift, reset de modificadores na desconexão/foco,
layouts e composição UTF-8. O modo HID I²C precisa ser avaliado contra o firmware
oficial: não presumir que todo pacote represente um relatório USB de seis teclas.

Fontes: [documentação A164](https://docs.m5stack.com/en/tab5/Tab5_Keyboard),
[firmware do teclado](https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW).

## Diagnóstico de eventos iniciado

Referência oficial fixada: M5Tab5-Keyboard-Internal-FW commit
539ec2a06d16b7ebc0ebfafa453d456a9dadc534. Modo Normal (0 em 0x10),
FIFO em 0x20: bit 7 indica pressionar; bits 6:4 linha (0–4),
bits 3:0 coluna (0–13); 0xFF é FIFO vazio. Um worker dedicado consulta
o contador em 0x02 e drena no máximo 32 eventos por ciclo de 10 ms.
O console expõe key_presses/key_releases/key_errors, sem registrar conteúdo.
Esta revisão é diagnóstico de eventos: ainda não fornece texto, layout, fila
para sessões ou recuperação completa de desconexão. Validação física: 102 pressionamentos e 102 solturas, sem erros I²C, com confirmação do usuário. Não comprova ainda layouts, repetição ou hot-plug.

## Campo de digitação inicial

Eventos brutos seguem por fila FreeRTOS de 64 entradas, com consumo limitado
no contexto LVGL. Overflow limpa a fila, incrementa erro e envia reset de
modificadores. O mapa físico inicial aceita ASCII, Aa mantido como Shift,
Sym mantido para símbolos, Backspace, Enter e Esc (limpa o campo de teste).
Ctrl gera códigos de controle no mapa; a tela não executa esses códigos.
Alt suprime texto nesta primeira versão. Não há Caps Lock por duplo toque,
composição de acentos, repetição, navegação por setas ou recuperação completa
após falha/desconexão. O campo tem limite de 256 caracteres e fica só na RAM.

Teste host: tests/keymap_test.cpp, compilado com SDK macOS 15.4 porque o linker
local não aceita os descritores do SDK 27 padrão. Passaram letras, release,
Shift, Ctrl+C, Backspace, símbolos e reset de modificadores. Build P4 aprovado.

Falhas I²C durante operação agora limpam a fila e os modificadores e entram
em tentativas de recuperação a cada 500 ms. Ao recuperar, o worker restaura
o modo Normal e limpa o FIFO do acessório antes de receber novos eventos.
Isso não substitui teste físico de remoção/reconexão; ausência desde o boot
ainda requer reiniciar a aplicação.

Ensaio no aparelho: usuário confirmou texto e retorno da digitação após
reconectar; oito pressionamentos e oito solturas foram observados. Não houve
erro I²C registrado nesse intervalo, de modo que não se considera validado
o ramo de tratamento de falha do barramento.

## Edição e repetição

Setas movem o cursor no campo de teste; Delete apaga à frente, Backspace
atrás. Ctrl+Esquerda/Direita vai ao início/fim do campo. Repetição de uma
tecla ativa: atraso 450 ms, intervalo 55 ms, sem rajada para compensar atraso.
Release, reset e overflow cancelam a repetição. Esc, Tab, Enter e códigos de
controle não repetem. Modificadores alteram o mapa sem produzir texto sozinhos.
O campo permanece um diagnóstico único, sem seleção por Shift+setas nem
roteamento geral de foco entre formulários.

Testes host adicionais verificam bordas dos temporizadores, soltura, reset,
setas, Delete e Ctrl+setas. Build P4 aprovado; teste físico em andamento.

Teste físico de edição concluído: usuário confirmou movimento do cursor,
Delete e repetição com parada ao soltar. A USB observou 42 pressionamentos e
42 solturas, zero erros I²C. Relatório: artifacts/editing-samples.json.

## Correção atual — acentos sem Alt

As seções anteriores são histórico. O mapa atual usa aspas simples como
tecla morta de agudo: `'` seguido de `a` produz `á`; `'` seguido de `c`
produz `ç`; `'` seguido de Aa+C produz `Ç`. Crase, til, circunflexo e aspas
duplas compõem seus acentos. Espaço ou repetição do acento imprime o literal.
Foco/Esc/reset cancelam a composição; hint exibe a pendência.
Testes C++ usam eventos brutos Normal de pressionar/soltar para á, ç e Ç,
além dos escapes. O teste LVGL injeta aspas e a pelo mesmo mapa. Isso não
substitui confirmação física da nova combinação.

## Trema pelo teclado físico

Segurar Aa, pressionar e soltar aspas simples e soltar Aa produz a tecla morta
de aspas duplas. Depois digitar a/e/i/o/u produz ä/ë/ï/ö/ü. Manter Aa ao digitar
a vogal produz Ä/Ë/Ï/Ö/Ü. Aspas duplas + Espaço imprime o literal.
Testes C++ cobrem todas as vogais por eventos Normal reais e o autoteste LVGL
exercita ü/Ü. A instrução também aparece no hint e na tela do cofre.

## Funções e terminal (revisão de 20/11/2026)

No teclado A164, mantenha **Aa/Shift** pressionado e use **1 a 0, - e +**
para **F1 a F12**, respectivamente. As teclas da linha numérica continuam
normais sem Shift. A camada é consumida ao enviar a função, inclusive no RDP;
Ctrl/Alt continuam disponíveis. Tab com Shift envia Shift+Tab no SSH.
As setas e funções são codificadas pelo libvterm conforme o modo solicitado
pelo programa remoto, incluindo application cursor (htop/curses).
