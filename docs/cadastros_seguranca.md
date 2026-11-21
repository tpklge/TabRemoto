# Etapa 3 — cadastros e segurança

O botão **Cadastros / cofre** abre formulários de conexões SSH/RDP, favoritos,
grupos, identidades reutilizáveis, segredos e decisões de confiança. Tudo executa
no P4; não é um cofre no Mac ou um serviço externo. Criar o cofre é uma ação
explícita do usuário: os testes não criam um cofre pessoal com senha conhecida.

A senha mestra tem exatamente **8 caracteres ASCII alfanuméricos**, conforme
pedido do usuário. Não se exige símbolo nem combinação de classes. Os campos
sensíveis são mascarados, limpos ao enviar/trocar de página e não são aceitos
como argumentos do console USB. Aa + aspas simples produz aspas duplas; soltar
Aa e digitar uma vogal compõe ä/ë/ï/ö/ü. Manter Aa na vogal produz a maiúscula.

## Cadastros

IDs aleatórios estáveis de 128 bits. Conexões: nome, protocolo, host/IP, porta,
grupo, favorito e identidade. Identidades: nome, usuário, domínio e método
(pedir credencial, senha salva ou chave salva), com referência ao segredo.
Excluir identidade/segredo vinculado é recusado; o usuário precisa remover
os vínculos primeiro. Alterações são validadas e salvas antes de serem aceitas.

Há limites de 128 registros por coleção e 64 KiB por geração do cofre;
valores secretos têm até 8 KiB. Segredos podem ser senha, conteúdo de chave,
passphrase, material VPN ou CA. Armazenar uma chave/CA não comprova formato,
algoritmo, passphrase correta ou capacidade de autenticação do futuro backend.
Não há cliente SSH/RDP operacional nesta etapa, nem importação automática de
arquivos de chave do SD. O conteúdo pode ser inserido no formulário protegido.

**Dados** importa JSON público schema 1, limitado a 8 KiB na interface. Não
sobrescreve IDs existentes, não importa confiança e recusa referências inválidas.
Exportação em `/TabRemoto/config/connections-g<geracao>.json`: somente conexões
e identidades; referências a segredos são removidas e o método volta a “pedir”.
Usuários/hosts continuam sendo metadados pessoais; o pacote público não os inclui.
Backup separado em `/TabRemoto/backups/vault-g<geracao>.trv` contém apenas a
geração criptografada. Ambas as operações recusam sobrescrever arquivo existente.

## Criptografia e formato

AES-256-GCM do mbedTLS do ESP-IDF 5.4.4 e scrypt do OpenSSL 3.5.9 já portado:
N=16.384, r=8, p=1, cerca de 16 MiB de memória de trabalho e limite de 24 MiB
na API. Salt aleatório de 16 bytes e nonces de 12 bytes. Uma chave de dados
aleatória é encapsulada por uma chave derivada da senha. Trocar a senha
reencapsula a mesma chave e grava nova geração; não transforma senha de servidor
em senha mestra. São algoritmos mantidos, sem implementação criptográfica própria.
A escolha limita o custo à RAM disponível no Tab5; não é garantia contra ataque
offline, especialmente se a senha alfanumérica de oito caracteres for previsível.

Cabeçalho de 148 bytes: magic/versão, geração, custo N, tamanho, envelope da
chave e nonce; o tag GCM autentica os 132 bytes de metadados e todo o payload.
Salt e parâmetros do envelope também são autenticados. Parser limita tamanho,
profundidade JSON, tipos, IDs, referências e versões antes de aceitar os dados.
Versões desconhecidas são recusadas sem salvar por cima; não existe formato
legado do cofre para migrar nesta primeira versão.

A fonte SAR de entropia do P4 é habilitada somente durante geração aleatória.
Um gate exclusivo impede sobreposição com o probe OpenSSL; o worker serializa
os jobs e não executa KDF ou I/O no callback LVGL. A prioridade 0 deixa o idle
executar sem desligar o watchdog. O formato cifrado é TRVAULT2, versão 2;
schema do cadastro/importação continua 1. Formato experimental TRVAULT1 não foi
usado para cofre pessoal e não é sobrescrito nem migrado silenciosamente.

Duas gerações ficam em `/TabRemoto/vault/store.a` e `store.b`. Cada gravação usa
temporário exclusivo, fsync, releitura comparada e rename para o slot inativo;
a geração ativa anterior permanece. Senha errada/adulteração não causa criação
de cadastro vazio. Recuperação de geração anterior exige ação explícita e
senha válida; pode restaurar cadastros e decisões de confiança antigos.
FAT/fsync não garantem atomicidade absoluta em queda de energia. Rollback por
substituição offline de arquivos não é impedido por contador externo confiável.
Backups antigos ainda podem ser abertos com a senha que valia naquela geração.

Bloqueio manual, após cinco minutos sem uso, SD ausente ou geração de montagem
alterada. Antes de gravar, o worker verifica novamente a geração sob o mutex
de I/O, impedindo carregar a chave desbloqueada para outro cartão.
Chave, JSON secreto, buffers de serialização e jobs usam zeroização resistente
a otimização antes da liberação. Core dump está desabilitado. Isso não promete
apagar todas as cópias transitórias feitas internamente pelo LVGL/allocator.
Não há sessão autenticada nesta versão; uma futura integração deverá definir
quais segredos o backend precisa reter durante a sessão.

## Confiança

Índice separado por protocolo, hostname/IP canônico e porta. DNS é convertido
para minúsculas e sem ponto final; IPs usam representação canônica. Aliases
não herdam confiança. Fingerprints SHA-256 são 64 caracteres hexadecimais.
Desconhecido exige decisão; alterado retorna bloqueio. A confirmação mostra
anterior/novo e exige motivo, conferindo novamente o pin anterior no commit.
Histórico preserva até 128 decisões, com fingerprint anterior/novo e datas.
Sem sincronização de relógio, os timestamps do aparelho não são hora confiável.

A interface permite cadastrar pins manualmente; não afirma ter observado um
servidor. Pin RDP não equivale a cadeia validada por CA. Aceitar uma vez,
importar CA e impedir envio real de credenciais antes da decisão precisam ser
conectados ao backend quando SSH/RDP existirem; atualmente não se envia nada.

## Testes

`tests/security_host`: KDF, AEAD, senha incorreta, senha fora do formato,
referências, exportação/importação, backup, troca de senha, trust por endpoint,
mudança de fingerprint, corrupção, recuperação explícita e falha de escrita.
Build host com AddressSanitizer e UndefinedBehaviorSanitizer.

`tools/check_security.py`: testes no SD em diretórios exclusivos de cache,
incluindo criação de favorito/identidade/chave de teste, bloqueio/desbloqueio,
alteração de senha, adulteração e recuperação, trema e formulários LVGL e
persistência do cadastro de teste após reboot real pelo Launcher. Não usa nem
sobrescreve arquivos do cofre pessoal. Relatório em artifacts/security-check.json.
Confirmação física dos novos formulários e corte de energia durante escrita
são validações separadas.

A tentativa inicial de PBKDF2-HMAC-SHA-256 com 600.000 iterações foi medida em
cerca de 64 segundos por derivação no P4; as rodadas longas foram interrompidas
apenas após confirmar ausência de cofre pessoal. Uma adaptação cooperativa de
agendamento preservou o algoritmo, mas não resolveu o custo. A implementação
final preserva as rodadas scrypt upstream, sem a adaptação PKCS5. A tabela de
trabalho V agora e dividida em blocos de 64 KiB para N=16.384/r=8; N/r/p,
consumo total, formato do cofre e resultados da derivacao permanecem iguais.
O SDK local nao foi modificado.
A recuperação de backup anterior à troca de senha é testada com a senha antiga.

Rodada completa aprovada no Tab5: 30.354 ms para o conjunto criptográfico,
3.166 ms para criar o cadastro de persistência e 2.856 ms para abri-lo após
reboot real. Mac confirmou scrypt/AES-GCM, favorito e referências a partir
dos bytes efetivamente gerados no SD. Relatório: security-check.json.

## Integração das etapas 4/5

Conexões antigas permanecem compatíveis. Novos campos opcionais descrevem
transporte, referência VPN e VPN obrigatória; exportar mantém a obrigatoriedade
mesmo removendo referências privadas. Remoção de segredo VPN referenciado é
bloqueada. A lista mostra nomes/destinos/tipos após desbloquear, atualiza depois
de salvar e não exibe segredos. Abrir SSH selecionado usa o worker do cofre
para resolver identidade, segredo e pin. Chaves de teste Ed25519 OpenSSH com
passphrase foram usadas de verdade no P4. Wi-Fi e perfis WireGuard podem ser
guardados como segredos cifrados. Uso/limites em [rede_ssh.md](rede_ssh.md).

## Arquivos de chave e vínculo de identidade

Copie .pem/.ppk para `/TabRemoto/imports` no SD (ou
`/TabRemoto/transfers/uploads` / `downloads`). O seletor permite subpastas.
No cofre desbloqueado, Segredos → **Importar .pem / .ppk / .conf** salva o
conteúdo cifrado, preservando o original. Remova o original depois conforme
sua política de guarda: a importação não apaga nem cifra o arquivo de origem.
PEM/OpenSSH e PuTTY PPK v2/v3 aceitam RSA/ECDSA NIST/Ed25519, dentro do limite
de 8 KiB. PPK com Argon2 tem limites de memória/custo descritos em
correcoes_usabilidade.md; formatos fora dos limites retornam erro explícito.

Na página Identidades, selecione o host no campo correspondente antes de
salvar; a criação e o vínculo são gravados juntos. Identidades continuam
reutilizáveis. O host precisa existir primeiro. Os botões **Hosts salvos**
no SSH e no RDP mostram os perfis após desbloquear o cofre.
Para conexão manual SSH sem importar, **Chave .pem / .ppk** escolhe o arquivo,
e o campo senha/passphrase recebe sua passphrase quando necessária.
