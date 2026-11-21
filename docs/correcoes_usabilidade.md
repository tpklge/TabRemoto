# Correções de usabilidade — 20/11/2026

Solicitação atual: corrigir sobreposição IP/Gateway na rede, Fn no A164,
navegação SSH/htop, importação PEM/PPK no cofre, seleção de hosts, vínculo
identidade/host, WireGuard por arquivo com túneis simultâneos, travas ao abrir
páginas e descoberta do RDP. Ao final, listar cinco próximas etapas.

Implementação em validação:

- Layout de rede separa endereço Ethernet e explicação, sem ocupar a mesma
  linha; Wi-Fi usa DHCP e a seção Ethernet é identificada explicitamente.
- Shift+1..0/-/+ gera F1..F12. Setas, Tab e funções são codificados por libvterm
  no worker SSH, respeitando DECCKM/mode de aplicação e modificadores.
- Seletor de hosts nas telas SSH/RDP; quatro cartões de início, incluindo RDP.
- Importação de chaves/conf em diretórios explícitos: /TabRemoto/imports e
  transfers/uploads/downloads. Navegação/listagem e leitura fora da thread UI.
  Chave importada é armazenada cifrada; o arquivo original não é apagado.
- Identidade pode vincular um host existente no mesmo commit transacional;
  continua reutilizável por outros hosts. Não muda o schema do cofre.
- PPK 2/3 usa leitor/crypto PuTTY 0.83 upstream e escritor OpenSSH adaptado
  somente para RAM. RSA/ECDSA NIST/Ed25519; Argon2 até 16 MiB/100 passes/8 lanes,
  além de verificação de memória disponível. Sem subprocesso externo no Tab5.
- Até quatro túneis WireGuard independentes; parada mantém rotas bloqueadas;
  liberação exige confirmação explícita. CIDRs sobrepostos e endereços locais
  iguais são recusados; somente um provedor de DNS global por vez.
- UI não reconstrói listas a cada pacote e não atualiza telas ocultas. Entrada
  física é processada a cada 10 ms; atualizações pesadas limitadas a 50 ms.

Evidências de host aprovadas: keymap/F1-F12; libvterm application cursor,
Shift+Tab, Ctrl+seta e funções; 12 casos PPK (RSA/ECDSA/Ed25519, v2/v3, com/sem
passphrase), saída com mesma chave pública do PuTTYgen upstream, MAC adulterado
ou senha errada rejeitados; import sandbox/symlink/traversal/tamanho;
cofre com vínculo atômico e rollback de erro de gravação. Sanitizers onde
aplicáveis. Testes físicos ainda em andamento; candidata de desenvolvimento
usa hooks temporários. A versão final deve desativá-los e preservar o cofre.

Primeira candidata d2eb36ac0fa2 instalou com backup integral e preservação dos
outros apps, mas não abriu o console. O último log visível era do SD; JTAG
somente para leitura confirmou que o SD e os workers já estavam ativos. A
thread main aguardava InitOnce do WLog, reentrado pelo erro de atexit. A tabela
newlib estava com 32/32 entradas (__atexit0._ind=0x20), sem bloco adicional.
Correção: o erro de registro WinPR usa logger do SDK e retorna falha real;
OpenSSL inicializa com OPENSSL_INIT_NO_ATEXIT, conforme o ciclo de vida RTOS
por reset. Recursos de sessões continuam liberados explicitamente. Não houve
gravação de flash/bootloader pelo debugger. Aguarda repetição física.

Candidata 2 iniciou após a correção do logger: 15 autenticações físicas de
arquivo aprovadas (RSA/ECDSA/Ed25519, PEM/OpenSSH e PPK2/3 com/sem senha).
PPK com senha errada e MAC adulterado foram rejeitados antes da rede.
O servidor real recebeu os 27 bytes esperados de setas em modo de aplicação,
F1/F10/F11/F12 e Shift+Tab pelo mesmo dispatch de matriz A164.

O ensaio htop revelou starvation de CPU0 no renderizador LVGL: um label
alocado por caractere. JTAG somente leitura identificou taskLVGL em desenho
/liberação de glifos; console e worker SSH não estavam bloqueados.
A candidata seguinte agrupa caracteres por linha/cor/estilo, recorta linhas
fora da área visível e mantém avanço fixo da fonte monoespaçada. Sem desativar
watchdog ou reduzir segurança de transporte. Ajustes adicionais ampliam
espaço entre rótulos e campos e removem botões fora da página do cofre.

Candidata 3 (523084f55635): input-selftest completo aprovado, incluindo
geometria dos campos Ethernet e foco de todas as páginas. SSH por formulário,
confirmação de chave e marcador desenhado aprovados. Seletor PEM/PPK acionado
pelos botões reais: PPK v3 e v2 cifrados autenticaram, senha digitada pelo
mesmo dispatch A164 e Enter. Modal de hosts visível e cancelável.

Dois arquivos .conf selecionados pela UI iniciaram dois peers WireGuard reais
simultâneos no Tab5: handshake e echo TCP em 10.87.0.1 e 10.88.0.1, autenticação
SSH por cada perfil nomeado, parar somente um sem interromper o outro,
rejeição de CIDR sobreposto, reconexão independente e parar todos mantendo
bloqueio de rotas aprovados (usability-ui-multivpn-device.json).
Há quatro slots; o ensaio físico exercitou dois. DNS global é único e rotas
sobrepostas não são suportadas. Cofre pessoal não foi desbloqueado nem alterado.

A captura de histórico SSH pela USB revelou ainda truncamento de respostas
JSON longas pelo buffer TX limitado. Respostas do console passam a ser
fracionadas com yield de um tick entre blocos; sem repetir ações de rede.
Essa falha de coleta é distinta do watchdog observado na candidata 2.

Candidata 4 (edc9492901ea) repetiu o caminho SSH real. Com a coleta USB
fracionada, os 27 bytes de teclas foram conferidos e htop atualizou por
mais de 12 segundos: setas, Shift+0/F10, saída ao shell e comando posterior
aprovados, sem task_wdt no log privado. Evidência:
usability-terminal-keys-device.json. Isso é uma observação curta, não teste
de duração. O custo máximo de callback registrado durante o autoteste que
abre dez telas de uma vez não representa latência normal de cada tela.

No ensaio de htop da candidata 4, 13 amostras de status cobriram 20,59 s,
com mínimo de 215.619 bytes internos livres; callback UI máximo de 51.232 µs
(um acima de 50 ms). RDP UI também passou NLA, confirmação do certificado,
recepção de desktop e foco com a nova interface. Reinícios entre app e
Launcher às vezes deixam ESP-Hosted sem responder no Launcher; a recuperação
usada foi um ciclo limpo app sem rede → Launcher, sem atualizar o C6.

A captura real da página Rede foi recolhida pelo Launcher e inspecionada:
IP fixo, máscara, gateway e DNS aparecem separados, com campo e rótulo
inteiros e botão Aplicar IP na mesma linha, sem interseções. Captura privada
fora do pacote público. Wi-Fi usa DHCP; esses campos são da Ethernet.

A versão final 4af37c7c0c8c (5.401.120 bytes) exigiu novo plano de espaço do
Launcher. Backup integral de 16 MiB verificado no SD e no Mac, SHA-256
1057890bd980f0040786c2bf08a2969cd6957c478f62d5587ea7687b0a7019da.
Após o reboot, o cliente viu o worker web ocioso enquanto o instalador
interno ainda respondia ocupado e encerrou a coleta. Inspeção somente leitura
confirmou a instalação automática concluída; nenhum execute foi repetido.
Cliente corrigido para continuar aguardando o estado ocupado independente,
com teste de regressão (17 testes Python aprovados). Reconciliação confirmou
cofre byte a byte e descritores dos outros apps preservados antes do boot.

Produção: `usability-release-rdp.json` aprovou diagnósticos desativados,
hooks recusados, pin alterado rejeitado antes de autenticar, NLA/TLS,
17 frames de desktop e observação de 25 s sem reboot (mínimo 7.767.936 bytes
PSRAM livre). A primeira ferramenta SSH tentou usar ssh-input, que está
corretamente bloqueado na versão de uso; nenhum erro do backend SSH.
O ensaio foi adaptado para comando remoto com PTY próprio, htop por 12 s e
encerramento do processo de ensaio: `usability-release-ssh.json` aprovado.
A navegação física/F10 e retorno ao shell foram testados antes na candidata
correspondente com o mesmo código de produto.

Conferência final aprovada em usability-final-preservation.json: cofre pessoal
byte a byte igual ao início da revisão, outros apps presentes sem mudança
de descritor, 17 arquivos descartáveis removidos do SD e servidores de
laboratório encerrados. Tela inicial pronta, Wi-Fi conectado, sem diagnósticos
e cofre bloqueado. Captura final SSH recolhida privadamente e inspecionada:
fonte monoespaçada regular, cores e grade do htop visíveis. Artefatos pessoais
e backups não fazem parte do pacote.
