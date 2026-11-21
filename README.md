# TabRemoto

RDP, SSH, arquivos e VPN no seu Tab5. Projeto ESP32-P4 em desenvolvimento,
com requisitos em [TabRemoto_prompt_completo.md](TabRemoto_prompt_completo.md).

A versão experimental 0.1.0 inclui display LVGL, teclado oficial com acentos,
recursos no SD, cadastros e cofre AES-GCM/scrypt. SSH por senha e chave Ed25519
com passphrase (incluindo arquivos PEM e PuTTY PPK 2/3), SFTP/SCP e WireGuard sobre Wi-Fi passaram em ensaios reais no
Tab5. O terminal usa libvterm, cores ANSI e fonte regular externa; a sessão
SSH ocupa quase toda a tela. Arquivos têm fila, progresso e cancelamento,
com nomes parciais exclusivos e publicação final sem sobrescrita.
RDP nativo FreeRDP/WinPR autenticou por NLA/TLS e renderizou desktop real de
Windows Server 2022 no Tab5, com teclado/ponteiro e pin. Clipboard Unicode,
resolução durante a sessão e RDP por WireGuard IPv4/IPv6 passaram no mesmo
servidor autorizado; isso não valida toda a matriz Windows.
A interface RDP tem barra lateral com ícones e toque como touchpad relativo:
deslizar move, tocar e soltar clica e segurar antes de mover arrasta.
O toque parado tolera tremor e nao depende de um limite curto de tempo; a seta
na barra envia clique esquerdo. Veja [a correcao do clique](docs/rdp_cliques.md). Os avisos de
resolução são discretos e preservam o desktop; selecionar o tamanho atual é
uma operação nula. Veja [a revisão da interface](docs/rdp_interface.md).
Wi-Fi oferece busca e redes salvas: redes protegidas reconectam no boot e
apos quedas; abertas exigem selecao manual. Certificados RDP aprovados ficam
salvos por host/porta, inclusive com o cofre bloqueado. A imagem RDP usa RGB565
e atualizacoes parciais. O scrypt do cofre mantem os parametros e usa paginas
menores para tolerar fragmentacao; pode pausar/retomar o RDP para liberar memoria.
Detalhes e evidencias: [Wi-Fi, confianca e desempenho](docs/wifi_confianca_desempenho.md).
Baseus AX88179A CDC-NCM foi identificado; link/DHCP/WireGuard por Ethernet
aguardam conexão Host disponível (USB-C do Tab5 é Device/recarga).
A versão de uso compila com CONFIG_TABREMOTO_DIAGNOSTICS desativado e não
mostra a tela Testes. Ensaios de desenvolvimento usam compilação separada.
Shift+1..0/-/+ envia F1..F12. Hosts salvos podem ser selecionados no SSH/RDP;
identidades podem ser vinculadas a um host. Importação de chaves e .conf pelo
seletor do SD e dois túneis WireGuard simultâneos foram validados no Tab5.
Até quatro perfis WireGuard são implementados. DNS fica vinculado ao perfil;
rotas sobrepostas exigem seleção inequívoca, sem liberar fallback ao parar.
O seletor Sessoes e Ctrl+Tab alternam terminais SSH e desktops RDP. Há quatro
contextos SSH e dois RDP, sujeitos à memória; quatro SSH e um RDP simultâneos
foram validados. Backup cifrado do cofre pode ser restaurado por arquivo .trv,
mantendo a senha atual após autenticar a senha do backup.
Uso e limites: [docs/rede_ssh.md](docs/rede_ssh.md) e
[correções de usabilidade](docs/correcoes_usabilidade.md).

O estado de validação atualizado está em [docs/status.md](docs/status.md).
O pacote `dist/TabRemoto-0.1.0/` contém binário, recursos, documentação e licenças;
os comandos abaixo usam a pasta completa do projeto, onde ficam os scripts.

## Compilar e empacotar

Requer ESP-IDF **5.4.4**, target `esp32p4`, GCC `esp-14.2.0_20260121`,
BSP Tab5 **1.3.2**, esp_lvgl_port **2.9.0**, LVGL **9.3.0**.
Os componentes transitivos estão fixados em `dependencies.lock`.

```sh
bash tools/build.sh build
bash tools/build.sh size
~/.espressif/python_env/idf5.4_py3.9_env/bin/python tools/package_release.py
```

O script usa Makefiles porque este Mac não tem Ninja. Ele ativa explicitamente
o Python 3.9 do IDF, evitando o Python 3.14 do Homebrew.
Adapte `IDF_PATH`/`IDF_PYTHON_ENV_PATH` para outra instalação.

## Instalar e testar no Tab5

Conecte a USB-C de manutenção com cabo de dados e deixe o LauncherdoPaludo
aberto, com Wi-Fi ativo e alcançável pelo Mac. Os scripts identificam Espressif
VID `303A`, PID `1001`; não confundem a serial BillBoard de um hub com o Tab5.
O Launcher usado neste aparelho é **0.8.0-rc.2**, com C6 **2.13.1**.

```sh
~/.espressif/python_env/idf5.4_py3.9_env/bin/python tools/launcher.py status
~/.espressif/python_env/idf5.4_py3.9_env/bin/python tools/launcher.py install
~/.espressif/python_env/idf5.4_py3.9_env/bin/python tools/device.py status
```

Os ensaios que injetam entrada ou executam autotestes, como `hardware_check.py`,
exigem uma build separada com `CONFIG_TABREMOTO_DIAGNOSTICS=y`. A versão de uso
recusa esses comandos. Resultados e procedimentos ficam em [docs/testes.md](docs/testes.md).

Para substituir somente uma versão já instalada deste projeto, use
`tools/launcher.py update`; ele confere a identidade TabRemoto, pede backup
do binário ao Launcher, remove apenas esse aplicativo e instala a nova imagem.

A USB autentica a sessão; os arquivos seguem pela API HTTP autenticada do
Launcher na rede local. O cliente reutiliza `../Tab5LauncherPaludo/tools/tab5_usb.py`.
Somente `build/TabRemoto.bin` é instalado. Recursos vão para `/TabRemoto/`,
e o binário de instalação fica em `/TabRemoto/updates/` no SD.
Se for necessário criar slot, o Launcher calcula o plano, faz backup completo e
preserva os apps existentes. O cliente também baixa/verifica uma cópia do backup
antes da alteração. Backups privados ficam em `artifacts/device-backups/`.

**Não execute `idf.py flash`, erase_flash, nem grave os endereços sugeridos pelo
build no aparelho com Launcher.** `partitions.csv` é apenas a referência de link
do projeto independente. O endereço real do aplicativo é escolhido pelo Launcher.
RESET ou `tools/device.py reboot` retorna ao Launcher; o app usa boot único.
Se uma instalação interromper, consulte a operação pendente no Launcher antes
de repetir; não substitua a infraestrutura de recuperação.

## Investigações reproduzíveis

```sh
bash tools/fetch_research.sh
bash tools/probe_rdp.sh
cd research/protocol_probe
bash ../../tools/build.sh -DIDF_TARGET=esp32p4 build
```

OpenSSL 3.5.9 e FreeRDP/WinPR 3.25.0 completos compilam e executam no P4.
O port inclui as adaptações POSIX/eventos e os canais cliprdr/disp nativos.
Fontes e patches são fixados em
`research/rdp-dependencies.json`. O build prepara e compila OpenSSL quando
ainda não existe; para reconstruí-lo: `python tools/build_openssl.py`.
O produto já integra WireGuard com seleção de interface física e múltiplos
CIDRs. Wi-Fi passou em handshake, payload TCP, SSH no túnel e bloqueio após
parar. A prova separada em research/protocol_probe permanece apenas um teste
de link; Ethernet ainda exige validação física. Veja [viabilidade](docs/viabilidade.md), [arquitetura](docs/arquitetura.md),
[armazenamento](docs/armazenamento.md), [segurança](docs/seguranca.md),
[teclado](docs/teclado.md) e [testes](docs/testes.md).

## Base do aparelho

A implementação da etapa 1 está descrita em [docs/base_aparelho.md](docs/base_aparelho.md):
composição de acentos UTF-8, Tab/Shift+Tab entre campos e botões, fonte/tema/textos
carregados do SD e remontagem assíncrona com preservação dos campos.
Teste automático dessa base: `python tools/check_base.py` no ambiente IDF.
Acentos sem Alt: aspas simples + vogal (`á`); aspas simples + `c`/`C`
produz `ç`/`Ç`. Também há crase, til, circunflexo e trema. Aspas + Espaço
ou duas aspas produzem o caractere literal. O hint mostra o acento pendente.

## Cadastros e cofre

**Cadastros / cofre** abre as telas de conexões/favoritos, identidades, segredos
e confiança. Crie sua própria senha mestra de **8 letras ou números**; os testes
automáticos usam pastas isoladas e não criam o cofre pessoal. Bloqueio manual,
inatividade de cinco minutos e ausência do SD impedem novas operações.
Detalhes, criptografia e limites em [cadastros e segurança](docs/cadastros_seguranca.md).
Teste no Tab5: `python tools/check_security.py` no ambiente IDF.

Trema: segure Aa, toque aspas simples, solte Aa e digite a vogal.
A sequência de desenvolvimento está em [próximas etapas](docs/proximas_etapas.md).

## Ensaios de SSH e VPN

`tools/check_ssh_files.py` recebe JSON privado por stdin sem eco, usa o envelope
USB de uso único e testa somente arquivos exclusivos, com limpeza e SHA-256.
Nunca passe senha em argumento de processo nem registre stdin.
`tools/check_lab_security.py` testa pin alterado/passphrase incorreta apenas
no servidor de laboratório. `tools/check_wireguard.py` exige tráfego real,
SSH com chave dentro do túnel e bloqueio após parar.

O peer/servidor próprio está em `research/wg_lab`; requer Go compatível com
seu go.mod. Execute a partir dessa pasta com um IP LAN do Mac:
`go run . ../../artifacts/device-private/lab-credentials.json "$TABREMOTO_LAB_IP"`.
Chaves de teste são geradas no início e salvas com permissão 0600 no diretório
privado. O ensaio desta entrega encerrou o servidor e removeu seus arquivos
de configuração privada; para repetir, inicie uma nova fixture primeiro.
O peer usa netstack userspace: não exige TUN, alteração de rotas do Mac ou
configuração do servidor SSH do usuário.

## Relatar problemas

Esta versao e experimental. Envie problemas e sugestoes em
[Issues do GitHub](https://github.com/tpklge/TabRemoto/issues), informando a versao,
o modelo/revisao do Tab5, os passos para reproduzir e o resultado esperado.
Consulte [o estado de validacao](docs/status.md) e os limites documentados antes
de testar. Nao publique senhas, chaves privadas, arquivos do cofre ou backups da
flash; remova dados pessoais dos logs e capturas.

O codigo deste projeto segue a [GPL-3.0](LICENSE). As dependencias mantem suas
licencas e avisos em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
