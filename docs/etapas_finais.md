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

Durante a observacao, houve 0 novos ticks de UI acima de 50 ms.
O maior tick acumulado desde o boot/abertura foi 2078.1 ms; isso
nao representa latencia constante nem comprova ausencia de pausas na abertura.

## Uso e limites

Sessoes abre o seletor; Ctrl+Tab/Ctrl+Shift+Tab alternam conexões. Cada aba tem
fila/terminal/frame próprios e indica transporte/perfil. Desconectar uma aba
preserva as demais. Fechar a tela preserva sessões ativas.

No RDP, Clipboard anuncia ou recebe texto Unicode de até 4096 bytes em RAM.
Resolucao oferece 1280x720, 960x540 e 800x450. Iniciar no maior tamanho desejado
permite reduzir e retornar sem realocar superfícies. Crescer pode ser recusado
pela fragmentação; o aviso não encerra a conexão. Reconexão tem até três
tentativas por queda; falha de senha/pin/logoff não provoca repetição de login.

Na página Cofre, Backup criptografado grava .trv em /TabRemoto/backups.
Para restaurar, desbloqueie o cofre atual, digite a senha do backup no campo,
selecione o arquivo e confirme. Somente dados autenticados substituem os
cadastros, mantendo a senha atual de oito letras/números. Feche sessões se
faltarem blocos de memória para a KDF; parâmetros de segurança não são reduzidos.

WireGuard aceita até quatro perfis com um peer por perfil, endereços IPv4/IPv6,
até oito rotas e dois DNS. A rota mais específica prevalece; empate sem perfil
explícito é recusado. Parar um perfil preserva sua proteção contra fallback.
DNS é resolvido por socket/canal do perfil escolhido; nenhum DNS global de VPN.
IPv6 interno foi testado sobre Wi-Fi/UDP externo IPv4. Relays dos ensaios RDP
somente copiaram bytes para .102; não traduzem TLS/RDP nem integram o produto.

## Evidências

| Ensaio real | Relatório em artifacts/ | Resultado |
|---|---|---|
| 4 SSH + 1 RDP; seletor/isolamento | remaining-sessions-device.json | Passou |
| Clipboard Unicode/resolução | remaining-rdp-clipboard-device.json | Passou |
| Crescimento recusado sem perder desktop | remaining-rdp-growth-device.json | Passou |
| Quatro quedas/reconexões/novas pinturas | remaining-rdp-reconnect-device.json | Passou |
| Clipboard após as quatro reconexões | remaining-rdp-clipboard-after-reconnect.json | Passou |
| DNS distinto/rotas sobrepostas/IPv6 SSH | remaining-advanced-network-device.json | Passou |
| DNS de full tunnel sem fuga física | remaining-dns-scope-device.json | Passou |
| RDP dentro de WG IPv4/IPv6 | remaining-rdp-vpn-device.json | Passou |
| Cofre descartável no SD/recuperação | remaining-vault-clean-device.json | Passou |
| Persistência real após reboot | remaining-vault-boot-device.json | Passou |
| Carga/estabilidade/remontagens SD | remaining-stability-device.json | Passou |
| Recibo após ACK descartado no host | remaining-provision-receipt-device.json | Passou; uma submissão |
| Release sem hooks/SSH/pin divergente/RDP | remaining-production-device.json | Passou |
| Cofre e imagens dos outros apps | remaining-final-preservation.json | Passou |

Testes host incluem 22 testes Python, vault com falha de escrita, parser de
clipboard Unicode, política IPv4/IPv6, pacotes internos WG e sandbox de imports
com ASAN/UBSAN, e c-ares com dois servidores DNS isolados. Compilação P4 completa
com fontes/patches fixados, incluindo c-ares 1.34.9 e FreeRDP/WinPR 3.25.0.

Ctrl+Tab foi injetado como scancodes A164 no dispatcher real; não se afirma
acionamento físico automatizado. Remontagem SD usa BSP/VFS real; não equivale
à retirada do cartão. Dez minutos não substituem teste de longa duração.

## Falhas encontradas e corrigidas

Gate de crypto passou a permitir aquisição justa pelos workers. DNS privado
foi retirado do estado global e preso ao transporte/perfil. IPv6 interno WG
teve ajuste de fila de entrada, comprimento/filtro e link-local do netif.

FreeRDP mantém GDI e pode não chamar PostConnect ao reconectar: liberar o pool
antes desse retorno interrompia a imagem, embora a autenticação voltasse.
O pool agora é preservado; quatro quedas com estímulo de pintura passaram.

Buffers RDP retêm capacidade para redimensionar sem grandes realocações.
Crescimento reserva staging antes de pedir mudança remota; falha deixa o
estado anterior disponível e mostra aviso. KDF detecta falta de bloco grande.

A USB espera o prompt antes da próxima linha e abre a serial exclusivamente.
Provisionamento inclui ID autenticado e recibo consultável: perder ACK não
repete ação, apenas lê o resultado correspondente. Backups privados, capturas,
credenciais e fixtures não integram o pacote público.
