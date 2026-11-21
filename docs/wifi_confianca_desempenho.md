# Wi-Fi, confiança RDP e memória — 21/11/2026

## Uso

Em Rede, **Buscar redes** faz uma varredura no worker e abre a seleção. Escolher
uma rede protegida nova preenche o SSID para informar a senha. Depois de obter
IP, o aplicativo salva a rede. **Redes salvas** conecta sem redigitar a senha.
No boot e após quedas, procura redes conhecidas protegidas. Redes abertas,
inclusive as salvas, sempre dependem de seleção manual. Uma solicitação manual
tem uma janela de 30 segundos antes de nova busca automática, evitando disputar
a associação enquanto aguarda DHCP.

O botão **Confiar e salvar** do RDP só libera a autenticação após gravar e
conferir o pin. A confiança é vinculada a protocolo, host normalizado e porta;
permanece após desconectar, reiniciar e atualizar o aplicativo. Certificado
diferente bloqueia a senha. Pins já aprovados no cadastro também podem ser
incorporados à configuração persistente quando conferidos com o servidor.

## Persistência

O componente `tabremoto_profiles` serializa gravações em segundo plano. Redes e
pins ficam em `config/known-a.enc`/`known-b.enc`, sob `/TabRemoto` no SD, com
AES-256-GCM, nonce aleatório, geração autenticada, arquivo temporário,
sincronização e leitura de verificação antes de publicar a nova versão na RAM.
Há limites de 16 redes e 64 pins. Arquivo existente com autenticação inválida
bloqueia a carga, sem aceitar silenciosamente uma versão antiga da confiança.

A chave aleatória fica no namespace NVS próprio `tabremoto_cfg`. Isso permite
conectar com o cofre bloqueado e protege uma cópia isolada do SD; o mecanismo
não fornece criptografia da flash do aparelho. O driver Wi-Fi usa configuração
em RAM, sem compartilhar as credenciais do Launcher. Configurações privadas e
backups completos ficam fora do pacote público.

## Cofre

O erro observado tinha relação com fragmentação: havia 26.360.528 bytes livres
de PSRAM, mas o maior bloco era de 11.534.336 bytes. A tabela V do scrypt agora
usa páginas de 64 KiB para os parâmetros do cofre. Mantém N=16.384, r=8, p=1,
os 16 MiB de trabalho e o formato TRVAULT2; não reduz o custo criptográfico nem
usa o SD como memória de trabalho.

Quando falta memória total para a derivação, o worker pausa as sessões RDP,
libera os contextos e retoma as mesmas sessões após a operação. A sessão Windows
continua disponível no servidor. Credenciais de retomada ficam somente na RAM
e são limpas ao liberar a requisição. Desconexão explícita cancela a retomada.

## RDP e interface

O FreeRDP desenha a superfície principal em RGB565; os buffers de apresentação
e o LVGL usam o mesmo formato. Isso elimina a conversão de cada quadro completo.
Atualizações unem apenas as regiões modificadas, mantêm a geração de cada
buffer e limitam a apresentação a 30 Hz. Se o LVGL perdeu uma geração, redesenha
a imagem inteira. Resize limpa as regiões pendentes e mantém os limites da
nova geometria. A reserva conservadora da superfície principal permanece nos
caminhos de crescimento de resolução.

O cursor já é criado acima da imagem. Reordenar suas duas camadas a cada
atualização invalidava todo o desktop; essa reordenação foi removida. A imagem
agora invalida somente a região alterada quando possível. A barra de ícones,
touchpad, clipboard e troca de resolução foram preservados.

Na observação inicial, o máximo de cópia foi 138.232 µs na variante que convertia
BGRA32 para RGB565 e 58.638 µs no caminho RGB565 direto. São amostras de sessões
reais diferentes, sem controle idêntico de carga do Windows; não representam
uma comparação de FPS com o cliente do Mac. O backend continua usando o caminho
clássico de desenho FreeRDP no ESP32-P4.

## Validação

- `wifi-trust-perf-device.json`: busca real, seleção sem redigitar senha,
  conexão automática depois do reboot sem provisionar Wi-Fi, confiança gravada
  antes de NLA e reutilizada após reconexão e reboot. Arquivos do SD conferidos
  como cifrados, sem SSID, senha ou fingerprint em texto.
- `wifi-trust-perf-fragmented-kdf.json`: amostras reais durante o autoteste do
  cofre descartável, com RDP suspenso e blocos livres menores que 16 MiB.
  Criação, senha errada, rotação, adulteração, recuperação e backup passaram;
  RDP retomou e o cofre pessoal permaneceu bloqueado.
- `wifi-trust-perf-native-rgb565.json` e
  `wifi-trust-perf-sidebar-device.json`: NLA/TLS, desktop, coordenadas do cursor
  confirmadas pelo Windows via clipboard, toque relativo, arraste/cancelamento,
  vinte no-ops de resolução, 1280→960→800→1280, ícones/alinhamento, clipboard,
  remontagem do SD e 60 segundos sem reset/erro de resize. Captura real revisada.
- `wifi-trust-perf-scrypt-host.json`: 16 derivações comparadas com scrypt do
  host sem a adaptação, incluindo N/r/p distintos e senha/sal vazios. Quatro
  falhas intermediárias de alocação liberaram todas as páginas. Maior alocação
  de 65.536 bytes sob um alocador que recusa blocos acima de 128 KiB.
- `wifi-trust-perf-vault-host-test.log`: testes do cofre com o provider paginado
  e ASAN/UBSAN no código do cofre/mbedTLS. Conversão, cópia e limites de regiões
  também passaram com sanitizadores; 22 testes Python existentes passaram.

O teste de gestos injeta eventos no dispatcher real do Tab5, sem atuador físico
no sensor. A política de rede aberta foi conferida no código; não havia um AP
aberto de laboratório para ensaiar uma associação física. Retirada do SD e corte
de energia não foram simulados por uma remontagem lógica.

## Entrega da versão de uso

`wifi-trust-perf-production-device.json` passou com diagnósticos desativados,
recusa de hooks, reconexão Wi-Fi sem provisionamento, SSH, pin RDP divergente
recusado antes da senha e NLA usando o pin salvo sem enviá-lo de novo. Foram
observados 120 segundos estáveis. `wifi-trust-perf-final-preservation.json`
confirma binário instalado, cofre pessoal byte a byte e imagens dos outros apps
por backup integral de 16 MiB. O desktop real foi deixado conectado ao servidor
autorizado; o cofre pessoal continua bloqueado.
