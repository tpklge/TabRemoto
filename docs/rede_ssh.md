# Rede, SSH e arquivos — implementação em validação

O firmware integra libssh2/OpenSSL, ESP-Hosted/C6, ECM USB e WireGuard/lwIP.
As revisões e patches ficam em `research/network-dependencies.json` e
`patches/`. Compilar não comprova autenticação nem tráfego VPN; as evidências
no aparelho são registradas separadamente em `docs/status.md`.

## Uso

Em Cofre, desbloqueie com sua senha de oito letras/números. As páginas mostram
nomes dos registros salvos, destino/usuário/tipo, sem revelar os segredos.
Carregar próximo seleciona o registro para edição. Em Conexões, Abrir SSH
selecionado abre o terminal; Conectar usa a identidade e o pin desse cadastro.
Identidade “ask” solicita senha apenas na RAM. Chave usa conteúdo protegido
no cofre e passphrase informada no campo da sessão. Formato validado: chave
privada Ed25519 OpenSSH cifrada por passphrase. Também passaram importação PEM e PuTTY PPK 2/3 e autenticações no aparelho.
RDP NLA/TLS nativo está disponível; detalhes em rdp_compatibilidade.md.

Rede oferece Buscar redes, Escolher rede e Redes salvas. Uma associacao que
obtiver IP salva SSID/senha cifrados no SD. No boot e depois de quedas, procura
redes conhecidas protegidas e conecta automaticamente sem abrir o cofre.
Redes abertas exigem selecao manual, inclusive quando ja foram salvas; nao
entram no ciclo automatico. A configuracao Wi-Fi do driver fica apenas na RAM,
sem reaproveitar credenciais NVS do Launcher. O primeiro ciclo de reconexao
mantem as esperas de 1/2/4/8/16 segundos, seguido de nova busca periodica.
Sessoes SSH interrompidas exigem nova conexao explicita. Os perfis Wi-Fi do
cofre continuam disponiveis para quem ja os usa.

Redes conhecidas e pins de certificado RDP usam AES-256-GCM em dois arquivos
`config/known-a.enc` e `known-b.enc`, com geracao autenticada, escrita temporaria,
sincronizacao e leitura de verificacao antes de publicar a nova configuracao.
A chave aleatoria de 256 bits fica no namespace proprio `tabremoto_cfg` da NVS;
nao se armazena senha Wi-Fi em texto na NVS. Essa chave permite reconectar com
o cofre bloqueado e protege o conteudo de uma copia isolada do SD; nao substitui
protecao fisica/criptografia da flash. Limites: 16 redes e 64 pins RDP.

O terminal usa libvterm, PTY xterm, UTF-8, cores SGR e até 128 linhas de
scrollback, com fonte regular externa. Modo de cursor da aplicação é
respeitado; setas/Fn e htop foram exercitados em PTY real. Não se declara
compatibilidade completa com todos os programas vim/tmux.
Enter no formulário inicia SSH; depois da autenticação o foco vai para o
terminal e Enter segue para o servidor. Chave desconhecida abre confirmação
visível antes de enviar a senha. Erros de cadastro aparecem na própria tela.
Ctrl e setas seguem para a sessão; Ctrl+Tab navega localmente.

Arquivos usa a sessão SSH autenticada. Listagem é SFTP; envio e download
identificam SFTP ou SCP. Arquivos locais ficam restritos a um nome simples em
`/TabRemoto/transfers/downloads/`; até 64 MiB por operação, tempo limitado.
Download escreve temporário exclusivo, sincroniza e conclui sem sobrescrever.
SFTP usa criação remota exclusiva. SCP verifica existência por SFTP antes de
enviar; o protocolo legado não oferece a mesma exclusividade atômica.
Desconectar cancela a sessão e a operação. Não há exclusão recursiva.
Uma operação prende a montagem FAT enquanto tem descritor aberto, impedindo
retry/remontagem entre blocos. Remoção física durante escrita segue pendente.

## Confiança e isolamento

Nenhuma senha é enviada antes de conferir a chave de host. Sem pin, há decisão
explícita de aceitar uma vez. Mudança de chave bloqueia autenticação até decisão.
Pins permanentes são mantidos na página Confiança do cofre por host e porta.
A seleção moderna de algoritmos não habilita assinatura RSA/SHA-1.
Bloquear o cofre impede novas autenticações por cadastro; sessão já autenticada
mantém o material necessário até desconectar. Conteúdo do terminal não vai ao SD.

Cada conexão permite transporte auto/Wi-Fi/Ethernet e perfil VPN obrigatório.
O destino é resolvido e conectado pela interface/perfil escolhido, com lease
por sessão. Parar outro túnel não encerra conexões válidas. Queda da rota da
própria sessão SSH exige nova conexão explícita; RDP pode tentar reconectar
até três vezes por queda. Não há migração silenciosa para outra rota.

WireGuard aceita até quatro perfis, cada um com um peer, endereços IPv4/IPv6,
até oito AllowedIPs, até dois DNS, MTU e keepalive. Usa criptografia upstream.
O UDP externo fica preso à interface física; o filtro de origem dos pacotes
internos confere AllowedIPs. IPv6 dentro do túnel passou no aparelho sobre
transporte físico IPv4; endpoint/transporte físico IPv6 ainda não foi validado.

A seleção automática usa o prefixo mais específico; empate entre perfis
bloqueia a conexão. Um perfil explícito só usa suas próprias rotas e DNS.
Rotas protegidas continuam bloqueadas quando o perfil para: não se permite
fallback por uma rota menos específica. Liberar a proteção exige ação explícita.
DNS tem canal e cache independentes por consulta/perfil, com socket preso à
interface. Perfil que cobre o destino DNS físico sem fornecer DNS próprio
bloqueia a consulta. Nomes privados iguais podem resolver para destinos
diferentes em túneis distintos, inclusive respostas somente AAAA.

Seletor Sessoes e Ctrl+Tab/Ctrl+Shift+Tab alternam até quatro contextos SSH e
dois RDP. Abrir mais sessões depende da memória disponível. O teste simultâneo
cobriu quatro SSH e um RDP; dois desktops independentes não foram validados
com a única conta/servidor disponível. Cada terminal/frame e fila são próprios.

## Manutenção e ensaios

`tools/provision_device.py` recebe JSON privado por stdin e cifra configuração
em RAM por AES-GCM com desafio USB de uso único (120 s); nenhum segredo entra
em argumentos de processo, pacote público, NVS ou log. Não habilitar eco do TTY.
`research/wg_lab` fornece peer Go userspace e servidor SSH de teste com chaves
geradas localmente. É um servidor de laboratório, não dependência do produto.
Material privado fica somente em `artifacts/device-private/`, fora do pacote.

## WireGuard por arquivo e múltiplos túneis

A página Rede → WireGuard abre `.conf` pelos mesmos diretórios de importação.
Há quatro slots, com seleção de transporte por perfil. Dois túneis Wi-Fi
simultâneos passaram handshake, TCP e SSH reais no Tab5 em 20/11/2026.
Dois perfis com rotas IPv4/IPv6 e nomes DNS sobrepostos passaram quando
selecionados explicitamente. Ambiguidade sem perfil foi recusada e parar um
perfil preservou sessões no outro. Essa validação não cobre quatro peers
simultâneos ou Ethernet física, que depende de uma porta USB Host disponível.
