con# TabRemoto — prompt completo de desenvolvimento

Copie o conteúdo a partir de “Início do prompt” para o Codex no VS Code, aberto na pasta do projeto.

## Início do prompt

Você é um engenheiro de firmware embarcado com experiência em ESP32-P4, ESP-IDF, interfaces gráficas, USB Host, SSH, RDP e redes. Desenvolva um aplicativo real para o M5Stack Tab5 chamado **TabRemoto**.

**Descrição do produto:** “RDP, SSH, arquivos e VPN no seu Tab5”.

O objetivo é transformar o Tab5 em um terminal portátil de administração de computadores e servidores: área de trabalho remota por RDP, terminal SSH, transferência SCP/SFTP, favoritos, identidades reutilizáveis e acesso por VPN WireGuard, usando Wi-Fi ou adaptador Ethernet USB compatível. A interface deve ser bonita, prática e inteiramente utilizável com toque e com o teclado físico oficial do Tab5.

Execute o desenvolvimento no repositório disponível. Leia primeiro as instruções locais e examine o código existente, preservando funcionalidades e alterações do usuário. Se a pasta estiver vazia, crie o projeto. Tome decisões rotineiras autonomamente, registre as escolhas e avance até uma entrega compilável e verificável dentro dos recursos disponíveis. Não entregue apenas um plano ou telas demonstrativas.

### 1. Regras fundamentais e limites de engenharia

1. O alvo é o **M5Stack Tab5 com ESP32-P4**, não um tablet Android, Raspberry Pi ou computador Linux. Não baseie a solução em APK, Electron, navegador completo ou pacotes disponíveis apenas em sistemas operacionais de desktop.
2. A documentação do Tab5 informa 16 MB de flash, 32 MB de PSRAM, tela IPS de 5 polegadas e 1280 × 720 pixels, módulo ESP32-C6 para Wi-Fi e slot microSD. Confirme a revisão da placa, do display e do BSP antes de escolher drivers e pinagens. Não configure overclock por padrão.
3. O teclado oficial **Tab5 Keyboard comunica por I²C**, conectado à porta de expansão, com interrupção e modos de operação próprios. Não presuma que seja um teclado USB. Teclados e mouses USB são periféricos adicionais.
4. SSH, SCP/SFTP, VPN e RDP devem executar no próprio dispositivo. Um gateway externo, Guacamole, proxy de conversão ou computador intermediário não pode ser uma dependência escondida. Uma eventual alternativa com gateway deve ser documentada separadamente e depender de decisão expressa do usuário.
5. RDP é o maior risco técnico. Investigue a portabilidade de uma implementação existente e execute uma prova de conceito cedo. FreeRDP é candidato, não uma biblioteca presumidamente pronta para ESP-IDF. Avalie suas dependências, WinPR, primitivas de sistema, criptografia, codecs, tamanho final e consumo de memória.
6. Não escreva implementações caseiras de RDP, SSH, TLS ou WireGuard para contornar dificuldades de integração. Reutilize implementações existentes e mantenha patches delimitados e documentados.
7. Não declare suporte a um recurso porque existe um botão, uma interface abstrata, um mock ou uma compilação sem erros. Distinga: planejado, implementado, compilado, testado em host e validado no Tab5 com servidor real.
8. Falta de hardware ou servidor impede afirmar validação física, mas não impede desenvolver, compilar, testar lógica portável e fornecer roteiro de validação. Registre precisamente o que falta e continue o trabalho independente.
9. Não substitua os requisitos difíceis por funções que retornem sucesso fictício. Um backend indisponível deve retornar erro explícito e aparecer indisponível na interface.
10. Não altere a configuração de segurança dos servidores, desative NLA globalmente ou queime eFuses do Tab5 para facilitar o desenvolvimento.

### 2. Plataforma e dependências

- Preferência: **ESP-IDF + CMake + FreeRTOS**, usando C para bibliotecas e C++ para a aplicação onde fizer sentido.
- Use um BSP oficial ou mantido para Tab5, depois de confirmar suporte à revisão física. M5Unified pode ser usado se a integração escolhida justificar isso.
- Use **LVGL** para a interface, em versão compatível com o BSP e o ESP-IDF escolhidos.
- Integre o ESP32-C6 pelo transporte e firmware de conectividade suportados pelo BSP, por exemplo ESP-Hosted quando aplicável. Documente a necessidade e a versão do firmware do C6. Não reprograme esse módulo automaticamente.
- Networking por esp-netif/lwIP e APIs realmente disponíveis na versão escolhida.
- Armazenamento no microSD com FATFS e operações de escrita protegidas contra falhas.
- Avalie **libssh2** ou **libssh** para cliente SSH, autenticação por chave e transferência. Escolha com base no port real, backend criptográfico, formatos de chave suportados, manutenção e licença.
- Avalie FreeRDP com cliente mínimo e renderização própria para RDP. Não compile backends de janela de desktop desnecessários. Compare mbedTLS e OpenSSL conforme a versão efetiva, inclusive limitações de autenticação; não presuma equivalência.
- Para WireGuard, examine uma implementação sobre lwIP/ESP-IDF e verifique suporte ao P4, à versão do SDK e a **Wi-Fi e Ethernet**. Não confunda a existência de um port antigo para outro ESP32 com compatibilidade comprovada.
- Fixe versões/commits das dependências e mantenha lockfiles. Documente patches locais, licenças, origem e atualização.
- Evite misturar stacks USB que tentem controlar o mesmo periférico. Mantenha um gerenciador único de USB Host para os drivers selecionados.

Antes de investir em telas detalhadas, produza `docs/viabilidade.md` com uma tabela de recurso, biblioteca, evidência, dependências, consumo estimado/medido, risco e estado. A análise deve orientar código e provas reais, sem virar uma etapa indefinida de pesquisa.

### 3. Organização do microSD e tamanho do firmware

**Tudo que não precisar fazer parte do executável deve ficar dentro de `/TabRemoto/` no microSD.** Não espalhe arquivos do aplicativo pela raiz do cartão nem em pastas de outros aplicativos.

O caminho acima é relativo à raiz do cartão. Se o ponto de montagem for `/sdcard`, o caminho físico será `/sdcard/TabRemoto/`. Centralize essa tradução.

Estrutura de referência:

```text
/TabRemoto/
  manifest.json
  config/
    app.json
    connections.json
    groups.json
    identities.json
    network.json
    ui.json
  vault/
    vault.meta
    secrets.enc
    keys/
    vpn/
  trust/
    known_hosts
    hostkey_history.jsonl
    rdp_certificates.json
    ca/
  assets/
    fonts/
    icons/
    themes/
    backgrounds/
  keymaps/
  locales/
  history/
    connections.jsonl
    transfers.jsonl
  transfers/
    downloads/
    uploads/
    partial/
  logs/
  cache/
  backups/
  updates/
```

Regras de armazenamento:

- Binário: código executável, drivers necessários, protocolos, criptografia e recursos mínimos para boot, recuperação e diagnóstico.
- MicroSD: fontes adicionais, ícones, temas, traduções, mapas de teclado, configurações, cadastros, históricos, certificados, chaves protegidas, transferências e demais recursos externos.
- Não proponha carregar bibliotecas dinâmicas ou executar código do SD como se isso estivesse disponível automaticamente no ESP-IDF. Bibliotecas compiladas necessárias aos protocolos permanecem no firmware.
- Evite duplicar conjuntos completos de recursos no binário e no SD. Mantenha apenas um pequeno fallback interno para informar problemas e orientar a recuperação.
- `manifest.json` deve indicar versão do pacote de recursos, schema e compatibilidade com o firmware. Valide caminhos e integridade; checksums detectam corrupção, mas não substituem autenticação de atualizações.
- Arquivos de metadados públicos não podem conter senhas, passphrases ou chaves privadas. Use identificadores que apontem para o cofre.
- Salve JSON de forma transacional com arquivo temporário, flush/sync conforme APIs disponíveis, backup e recuperação. FAT não oferece garantia absoluta contra perda de energia; implemente recuperação e documente esse limite.
- Valide schema, limites de tamanho e migrações antes de carregar os dados. Não sobrescreva um cadastro corrompido com um cadastro vazio.
- Implemente rotação e limites de logs e históricos. Evite escrever a cada evento de tecla ou a cada pacote de rede.
- Ausência de SD: abra uma tela funcional de diagnóstico e permita tentar montar novamente. SD somente leitura: informe o estado e não finja persistência.
- Remoção durante sessão: suspenda operações de disco, aborte ou pause transferências com segurança e bloqueie novas operações dependentes do cartão. Defina política explícita para sessões ativas, buffers e segredos.
- Na primeira inicialização, crie as pastas e configurações mínimas sem sobrescrever dados existentes. Distribua recursos completos em `sdcard/TabRemoto/` no pacote do projeto.
- Use NVS apenas para dados mínimos justificados pelo hardware/bootstrap, listados na documentação; não duplique ali o cadastro de favoritos ou o cofre.

### 4. Interface visual e navegação

Projete uma interface para **1280 × 720 em modo paisagem**, com textos legíveis, bom contraste, tema escuro inicial e tema claro opcional. Português do Brasil por padrão. A identidade visual pode usar o conceito de uma janela de terminal que também funciona como portal para um desktop remoto.

Evite telas densas demais. Use alinhamento consistente, hierarquia visual, ícones acompanhados de texto nos locais importantes e estados claros de carregamento, falha e conexão.

Telas necessárias:

1. **Início:** favoritos, recentes, conexão rápida, pesquisa e estado da rede/VPN.
2. **Conexões:** lista por grupos, filtro por protocolo, tags, favorito, duplicar, editar, exportar metadados e excluir com confirmação.
3. **Editor de conexão:** abas Geral, Credenciais, Rede/VPN e opções específicas do protocolo.
4. **Identidades e chaves:** usuários reutilizáveis, credenciais, importação de chaves, fingerprints e vínculos.
5. **Terminal SSH:** área útil ampla, scrollback, fonte ajustável e barra de teclas especiais recolhível.
6. **Área de trabalho RDP:** tela cheia, ponteiro, modos de toque e painel flutuante recolhível.
7. **Arquivos:** navegador local/remoto e fila de transferências.
8. **VPN:** perfis WireGuard, conectar/desconectar e diagnóstico.
9. **Rede:** Wi-Fi, Ethernet USB, interfaces, DHCP/IP fixo e diagnóstico básico.
10. **Confiança e histórico:** chaves dos servidores SSH, certificados RDP e eventos de alteração.
11. **Configurações:** aparência, teclado, armazenamento, privacidade, backup e informações técnicas.

A barra de status deve mostrar interface ativa, endereço IP, estado da VPN, presença de SD e bateria quando a leitura existir. Diferencie claramente “interface VPN criada”, “handshake confirmado” e “recurso remoto acessível”.

Toda operação demorada deve permitir cancelar quando tecnicamente possível. Rede, criptografia e disco não podem bloquear a thread de interface. Preserve os formulários quando uma tentativa de conexão falhar.

### 5. Teclado oficial, teclado USB e ponteiro

O teclado físico oficial é requisito central, não um detalhe opcional.

- Implemente o driver real do Tab5 Keyboard a partir da documentação oficial e dos exemplos compatíveis. Confirme endereço, registradores, IRQ, formato dos eventos e modo utilizado.
- Centralize os eventos em uma camada de entrada que receba teclado I²C, teclado USB HID e teclado virtual.
- Preserve eventos de pressionar/soltar e modificadores. Não reduza tudo a caracteres ASCII, pois RDP depende também de scancodes e combinações.
- Trate Ctrl, Alt, Shift, Tab, Esc, setas, Backspace, Delete, Home, End, PageUp/PageDown e F1–F12, incluindo atalhos ou uma barra para teclas ausentes fisicamente.
- Suporte layout físico real do acessório e mapeamento configurável. Ofereça configuração US/Internacional e ABNT2 quando adequada; não prometa teclas inexistentes no acessório.
- Texto local e SSH devem aceitar UTF-8, acentos e composição conforme o mapa implementado. No RDP, separe envio de texto Unicode de envio de teclas físicas e atalhos.
- Ctrl+C em terminal deve gerar o evento esperado pelo terminal. Copiar texto precisa ter comando separado, evitando conflito.
- No RDP, Ctrl+C/Ctrl+V, Alt+Tab, teclas Windows e Ctrl+Alt+Del precisam de encaminhamento ou comando dedicado. Diferencie atalhos locais e remotos de forma visível.
- Permita navegar por listas e formulários com Tab, Shift+Tab, Enter e Esc, mantendo o foco evidente.
- O teclado virtual deve abrir sob demanda e se recolher quando o teclado físico estiver disponível, sem atrapalhar a sessão.
- Valide digitação contínua, repetição, teclas simultâneas e desconexão sem modificadores presos. Ao perder foco ou conexão, libere teclas remotas pressionadas.
- Mouse USB opcional, toque direto e modo touchpad. No RDP: clique, duplo clique, botão direito, arrastar, rolagem e transformação correta de coordenadas com zoom e barras laterais.

### 6. Cadastro de conexões, favoritos e identidades

Modele **destino**, **identidade**, **segredo** e **perfil VPN** como entidades separadas, com IDs estáveis e referências verificadas.

Uma conexão deve permitir:

- Nome amigável, protocolo, hostname/IP, porta, grupo, tags e favorito.
- Identidade padrão e seleção de outra identidade antes de conectar.
- Usuário, domínio/UPN quando aplicável e método de autenticação.
- Referência a senha ou chave no cofre; opção de pedir a senha a cada conexão.
- Política de confiança do servidor, timeout e keepalive.
- Interface física preferida, VPN associada e indicação de **VPN obrigatória**.
- Resolução/qualidade e segurança do RDP; opções de terminal SSH; diretórios iniciais de arquivos.
- Último uso e resultado da conexão, sem armazenar segredos no histórico.

Uma identidade deve poder ser reutilizada em vários servidores. Cada destino pode ter várias identidades elegíveis. Excluir uma identidade ou chave exige mostrar os vínculos e impedir referências quebradas.

Inclua importação/exportação de cadastros sem segredos por padrão e backup criptografado completo como operação distinta. Não exporte credenciais acidentalmente.

### 7. Cofre, chaves e histórico de confiança

Implemente proteção real para credenciais e material criptográfico persistente.

- Na primeira configuração do cofre, solicite uma senha mestra. A senha de um servidor não deve ser a senha mestra automaticamente.
- Use AEAD de biblioteca mantida, por exemplo AES-GCM ou ChaCha20-Poly1305, com nonces únicos, salt, versão de formato e autenticação dos metadados relevantes.
- Use uma KDF adequada, disponível na plataforma, com parâmetros medidos no Tab5. Documente o custo e o limite de proteção contra ataques offline. Não derive a chave de MAC, número de série ou senha fixa.
- Separe a chave do cofre da senha mestra para permitir troca de senha com reempacotamento seguro quando a arquitetura escolhida permitir.
- Chaves SSH importadas, senhas RDP/SSH, passphrases e chaves privadas/PSK WireGuard devem ficar criptografadas no SD. Não retenha cópias de importação em texto puro dentro da pasta do aplicativo.
- Suporte importação de chave privada SSH protegida por passphrase. Documente exatamente os formatos e algoritmos aceitos pelo backend e pela configuração criptográfica.
- Priorize Ed25519 e RSA com SHA-2 quando efetivamente suportados; avalie ECDSA. RSA como tipo de chave não significa habilitar automaticamente assinatura `ssh-rsa` baseada em SHA-1.
- Bloqueio por inatividade, bloqueio manual e limpeza de segredos em RAM com primitivas resistentes a otimização, na medida suportada. Não grave core dumps com segredos sem proteção.
- Política explícita para sessões já autenticadas quando o cofre bloquear; novas autenticações ficam bloqueadas. Não prometa apagar segredos que o backend ainda precisa manter durante uma sessão.
- Informe que flash encryption/NVS encryption não criptografa automaticamente o microSD. Não habilite configurações irreversíveis de produção no aparelho sem instrução específica.

**Interprete “histórico de chaves” de duas formas e implemente ambas:**

1. Inventário das chaves do usuário: rótulo, tipo, fingerprint público, criação/importação, última utilização, vínculos e estado ativo/arquivado/revogado. Retenha eventos, não cópias desnecessárias de chaves privadas antigas.
2. Histórico das chaves de host SSH e certificados RDP: primeira observação, última observação, fingerprint anterior/novo, decisão de confiança e motivo informado pelo usuário.

Para SSH:

- Servidor desconhecido: mostrar fingerprint SHA-256, tipo de chave, hostname e porta; permitir aceitar uma vez, confiar de forma persistente ou cancelar.
- Chave alterada: bloquear a conexão automaticamente e exigir decisão explícita após exibir os dados anteriores e atuais.
- Indexar confiança por identidade do endpoint e porta, tratando aliases de forma explícita. Nunca aceitar qualquer chave por padrão.

Para RDP:

- Validar certificado conforme suporte do backend; permitir CA privada importada.
- Para certificado autoassinado, oferecer pinning explícito e informar que isso é uma decisão de confiança própria, distinta de validação por CA.
- Mudança de certificado/pin não pode passar silenciosamente.
- Credenciais não podem ser enviadas antes da decisão de confiança necessária.

Desabilite por padrão o registro do conteúdo do terminal. Se houver gravação opcional, informe o usuário, aplique retenção e explique que o conteúdo pode conter segredos. Logs operacionais devem remover senhas, tokens, passphrases e material privado de VPN.

### 8. Cliente SSH

Implemente um terminal interativo SSH funcional:

- Autenticação por senha e por chave privada; keyboard-interactive quando o backend permitir.
- Terminal VT100/ANSI com subconjunto xterm bem definido, sequências de escape, cores, cursor, seleção e scrollback limitado.
- PTY, negociação de tamanho e envio de resize quando fonte ou área útil mudar.
- Compatibilidade com shell, `top`, `htop`, `nano`, `vim` e `tmux` deve ser testada, sem afirmar emulação xterm completa antes da validação.
- Reconexão controlada e keepalive; deixe claro que reconectar cria outra conexão e não recupera automaticamente o estado de um shell perdido.
- Cancelamento de DNS, conexão e autenticação por política de timeout. Trate EAGAIN/nonblocking conforme a biblioteca.
- Seleção/copiar/colar texto com limites e tratamento de colagens multilinha; não execute comandos colados implicitamente quando for possível oferecer revisão.
- Caracteres UTF-8 podem chegar em blocos parciais: o parser deve preservar sequências incompletas entre leituras.
- Trate saída remota como dados não confiáveis: limites nas sequências ANSI, título de janela e outras extensões. Não habilite escapes capazes de escrever arquivos locais ou manipular clipboard silenciosamente.

Permita iniciar SSH e abrir arquivos no mesmo host reutilizando o cadastro. A reutilização da conexão autenticada deve respeitar as capacidades e limites do backend.

### 9. Transferência SCP e SFTP

O produto deve oferecer **SCP real** e **SFTP**, deixando o método visível. Não renomeie SFTP para SCP.

- Navegador de arquivos remotos via SFTP e navegador local restrito inicialmente a `/TabRemoto/transfers/`.
- Enviar e baixar arquivos usando SCP quando o servidor permitir o protocolo/comando remoto necessário; reportar quando ele não estiver disponível.
- Servidores OpenSSH modernos podem usar SFTP como transporte do comando `scp` de desktop. Isso não torna SCP legado e SFTP equivalentes; documente a diferença e permita selecionar o método.
- Fila limitada de transferências, progresso, bytes, velocidade aproximada, tempo estimado e cancelamento.
- Transferência em blocos, sem carregar o arquivo inteiro na memória.
- Arquivos parciais com sufixo próprio; só finalize o nome quando a operação terminar com sucesso.
- Verifique espaço, permissões e limites de arquivos/tipos numéricos. FatFS/FAT32 não aceita um arquivo individual de 4 GiB ou mais; o limite efetivo depende também da configuração e das APIs, e deve ser verificado.
- Retomada deve ser oferecida somente quando implementada e validada no método escolhido, em geral SFTP. Não anuncie retomada universal para SCP.
- Para sobrescrever, solicite confirmação; permita renomear ou pular.
- Proteja caminhos locais contra `..`, absolutos inesperados, nomes retornados por servidor malicioso e escape da pasta permitida. Trate symlinks e recursão com limites.
- Comandos remotos utilizados por SCP devem ter escaping correto. Não concatene nomes de arquivos não confiáveis em um shell sem proteção.
- Operações de excluir/renomear remotas exigem confirmação apropriada; pastas recursivas podem vir depois do fluxo de arquivo individual funcional.

### 10. Cliente RDP: compatibilidade de 2008 ao presente

**Requisito de produto:** conectar diretamente aos serviços RDP das versões do Windows Server desde **2008 e 2008 R2 até as versões disponíveis no momento da implementação**, incluindo 2012, 2012 R2, 2016, 2019, 2022 e 2025. Verifique a lista atual antes de fechar a matriz. Windows desktop que disponibiliza servidor RDP é alvo adicional.

Não interprete isso como implementar uma biblioteca diferente para cada Windows. Implemente negociação de capacidades e segurança e teste combinações reais. Compatibilidade depende também de atualizações e políticas do servidor.

Requisitos:

- Conexão RDP nativa, autenticação, recepção de imagem e envio de teclado/mouse.
- Usuários locais e de domínio, `DOMINIO\\usuario` e UPN quando suportado.
- **NLA/CredSSP**, TLS e autenticação necessária aos cenários de domínio, a partir das capacidades reais da biblioteca.
- Não considerar “RDP pronto” se só conectar com NLA desligado. Políticas que exigem Kerberos, restringem NTLM, exigem smartcard/MFA ou RD Gateway devem ser identificadas e documentadas; não alegar suporte automático a elas.
- Modo moderno seguro por padrão. Modo legado deve ser explícito e por conexão, com indicação dos protocolos/algoritmos efetivamente exigidos. Nunca tente downgrade inseguro silencioso após erro de NLA/TLS.
- Para Windows antigos, distinguir sistemas atualizados e não atualizados, versões de CredSSP e protocolos disponíveis no build criptográfico. Caso não haja interseção segura/implementável, informar a limitação concreta sem fingir que o requisito foi atendido.
- Resoluções iniciais: 1280 × 720 e alternativas menores para reduzir carga, respeitando a área disponível e as capacidades do servidor. Permitir escalar e mover uma resolução maior sem confundir coordenadas.
- Priorizar caminho gráfico compatível de menor custo, atualizações de bitmap/retângulos e negociação suportada. Não dependa obrigatoriamente de AVC/H.264/AV1, nem presuma que aceleradores do P4 decodificam qualquer codec RDP.
- Profundidade de cor configurável quando negociável e modo de baixa banda: sem papel de parede, animações e efeitos, conforme as capacidades do protocolo.
- Clipboard de texto opcional e desativável, somente se houver backend implementado. Áudio, impressora, smartcard, unidade redirecionada, RemoteApp, multimonitor e RD Gateway ficam como extensões posteriores, com estado visível.
- Gerenciar desconexão, encerramento e reconexão sem prometer recuperação se o servidor não a suportar.
- Começar com uma sessão RDP ativa por vez. Não manter múltiplos desktops decodificando em segundo plano sem orçamento medido.

**Prova de conceito obrigatória, cedo:** gerar build para ESP32-P4 que inicialize o backend, conecte a um servidor de laboratório com NLA/TLS, autentique, desenhe um desktop e envie uma tecla e um clique. Medir RAM interna, PSRAM, flash, tempo de conexão e responsividade. Uma prova executada em Linux não substitui essa prova no Tab5.

Se o port RDP bloquear:

1. Registrar as dependências e erros concretos.
2. Isolar o port em um componente e fazer adaptações reais, sem simular sucesso.
3. Continuar SSH, arquivos, persistência, teclado, rede e VPN quando independentes.
4. Deixar o RDP identificado como indisponível/experimental até passar na prova.
5. Não adotar gateway externo nem retirar o requisito sem autorização do usuário.

### 11. Rede Wi-Fi e placa Ethernet USB

**Wi-Fi:** configurar SSID/senha, conectar, reconectar e exibir sinal/IP. Guardar a senha com a política de proteção definida, documentando qualquer persistência indispensável no stack.

**Ethernet USB:** criar uma abstração de interface de rede e suporte a adaptadores por driver/classe efetivamente implementados.

- Priorize a avaliação de **USB CDC-ECM**, que possui driver no ecossistema Espressif, verificando compatibilidade com o SDK e o Tab5.
- Avalie CDC-NCM, RNDIS e chipsets vendor-specific apenas com evidência de driver ou plano de port. RTL8152/RTL8153, AX88772 e AX88179 são exemplos a investigar, não uma lista de compatibilidade garantida.
- Um adaptador vendido como “USB Ethernet” não é necessariamente CDC-ECM. Identifique VID/PID, interfaces, endpoints, classe e chip real.
- Implemente enumeração, link up/down, DHCP, IP fixo, máscara, gateway e DNS para o driver inicial.
- Mostre “adaptador reconhecido e compatível”, “dispositivo detectado sem driver” e “sem link” como estados diferentes. Enumeração USB não prova conectividade IP.
- Não prometa 100 Mb/s ou 1 Gb/s só pela especificação do adaptador; meça throughput no Tab5.
- Valide alimentação VBUS, corrente, hot-plug e estabilidade. Hub alimentado externamente pode ser necessário, conforme os periféricos.
- Permita teclado/mouse USB e Ethernet compartilharem um hub quando a stack e os drivers escolhidos suportarem isso. Teste a combinação, sem presumir suporte ilimitado a endpoints/dispositivos.
- O teclado oficial I²C deve continuar funcional enquanto a USB-A é usada pela placa Ethernet.
- Wi-Fi e Ethernet podem coexistir. Crie política de prioridade e seleção de interface, incluindo rota para o servidor VPN, DNS e reação a link perdido.
- Trocar de interface física normalmente quebra sessões TCP existentes; mostre isso e reconecte conforme política, sem prometer migração transparente.

Entregue `docs/usb_ethernet_compatibilidade.md` com driver, classe/chipset, VID/PID, modelo físico, versão testada, teste direto/hub e estado: investigado, portado, compilado ou validado. Entradas sem adaptador físico permanecem “não testadas”.

### 12. VPN: WireGuard inicialmente

Crie uma interface `VpnProvider` extensível, com **WireGuard como primeira implementação real**. Não adicione provedores fictícios para OpenVPN/IPsec.

Suporte:

- Cadastrar, editar, importar e remover perfis.
- Importar um subconjunto documentado de arquivo `.conf`: Address, PrivateKey, DNS, MTU, PublicKey, PresharedKey, Endpoint, AllowedIPs e PersistentKeepalive, conforme suporte real.
- Não executar `PreUp`, `PostUp`, `PreDown` ou `PostDown`. Não ignore silenciosamente opções importantes não implementadas; rejeite ou peça correção com explicação.
- Armazenar chaves privadas e PSK no cofre, nunca no JSON público.
- Um túnel ativo e um peer inicialmente são aceitáveis, com limites documentados. Perfis com múltiplos peers não podem ser truncados silenciosamente.
- Importação de arquivo deve evitar deixar uma cópia em claro na pasta de trabalho do aplicativo. Oriente a exclusão do arquivo original pelo usuário, sem apagar outros arquivos automaticamente.
- Funcionar sobre **Wi-Fi e Ethernet USB**, com teste independente para cada transporte.
- Não acoplar o provider a uma interface Wi-Fi fixa. Alguns ports existentes de WireGuard assumem Wi-Fi e precisam de adaptação; verificar o código, não apenas o README resumido.
- Split tunnel por CIDR e, se o backend/roteamento permitir, full tunnel. Não reduza AllowedIPs a um único endereço se o perfil precisa de várias redes.
- A rota de transporte até o endpoint WireGuard deve permanecer na interface física; evite enviar o próprio transporte de volta para o túnel.
- Configure e restaure DNS de forma explícita. Resolução do endpoint acontece com conectividade física apropriada; domínios privados dos destinos usam a política VPN definida.
- Informe estado do túnel, último handshake, bytes e falhas; tenha teste de acesso a um destino configurável, pois um handshake sozinho não prova que RDP/SSH será alcançável.
- Ajuste MTU com validação e documente problemas de fragmentação/PMTU. Não presuma um valor universal.
- Sincronize e valide o relógio conforme requisitos da implementação, usando RTC e SNTP quando disponíveis. Não bloqueie a arquitetura em NTP público se o cenário usa rede isolada; permita acerto manual e fonte configurável.
- Se uma conexão exige VPN, aguarde um túnel utilizável e bloqueie o destino quando ela cair. **Nunca reconecte diretamente pela rede física como fallback dessa conexão.**
- Um bloqueio por aplicativo não é necessariamente um firewall completo. Para full tunnel, prove ausência de fuga IPv4/IPv6/DNS no escopo anunciado; se IPv6 não estiver implementado de forma segura, desabilite seu uso nesses perfis e declare o limite.
- Não anuncie kill switch só por mudar a rota default. Teste a queda do túnel, DNS, interfaces simultâneas e restauração de rotas.
- Com a perda da rede, atualizar estado e reconectar com backoff limitado, sem travar a UI nem vazar acesso fora da VPN.

### 13. Arquitetura e dados

Separe a aplicação em componentes com responsabilidades claras:

```text
main/
components/
  tabremoto_board/
  tabremoto_input/
  tabremoto_ui/
  tabremoto_storage/
  tabremoto_vault/
  tabremoto_profiles/
  tabremoto_network/
  tabremoto_usb/
  tabremoto_vpn/
  tabremoto_ssh/
  tabremoto_terminal/
  tabremoto_files/
  tabremoto_rdp/
  tabremoto_sessions/
  tabremoto_diagnostics/
sdcard/TabRemoto/
docs/
tools/
tests/
```

Adapte os nomes ao projeto existente, preservando a separação. Bibliotecas externas não devem ser copiadas e modificadas indiscriminadamente; use dependências fixadas e patches rastreáveis.

Defina modelos para `ConnectionProfile`, `Identity`, `SecretReference`, `SshKeyMetadata`, `KnownHostRecord`, `HostKeyEvent`, `RdpTrustRecord`, `NetworkProfile`, `WireGuardProfile`, `SessionState` e `TransferJob`.

Requisitos de arquitetura:

- Eventos e filas limitadas entre UI, sessões, rede e disco.
- Propriedade clara de cada sessão, socket, buffer e recurso criptográfico.
- Máquina de estados explícita: ocioso, resolvendo, preparando VPN, conectando, validando servidor, autenticando, conectado, reconectando, cancelando, desconectado e falha, quando aplicável.
- Cancelamento que realmente alcance operações em andamento e libere recursos.
- Evitar chamadas de LVGL fora do contexto/lock correto do port.
- APIs de erro tipadas com mensagem útil ao usuário e detalhe sanitizado para diagnóstico.
- Uma conexão de alto custo por vez inicialmente, com política de coexistência de SSH, transferência e RDP baseada em medições. Não prometa sessões ilimitadas.
- Não retransmitir senhas da UI para logs ou barramentos de eventos genéricos.

### 14. Memória, desempenho e integridade

- Não trate 32 MB de PSRAM como 32 MB de heap interno ou memória DMA equivalente.
- Faça orçamento de RAM interna, PSRAM, buffers de vídeo, caches de RDP, terminal, filas, USB, rede e criptografia.
- Referência de cálculo: um framebuffer 1280 × 720 RGB565 usa 1.843.200 bytes; dois usam 3.686.400 bytes, antes de alinhamento e demais buffers. Com 32 bits por pixel o custo dobra. Não aloque framebuffers redundantes sem necessidade.
- Prefira atualização por retângulos e buffers limitados. Evite redesenhar toda a tela para cada tecla ou pacote quando o port permitir atualização parcial.
- Limite scrollback, caches e transferências. Use streaming e filas com backpressure.
- Meça heap livre, menor heap, maior bloco livre, consumo de PSRAM, stack high-water marks e falhas de alocação.
- Registre tempos de conexão, latência de entrada e throughput medidos, com cenário, resolução, rede e VPN utilizados. Não invente FPS ou velocidade.
- Defina uma meta inicial verificável de uso interativo para administração estática, sem prometer vídeo fluido ou experiência de desktop de alta performance.
- Rotinas de reconexão, TLS e transferência devem ceder CPU. Não desabilite watchdog para ocultar bloqueios.
- Ao reduzir memória disponível, negue novas operações com mensagem clara em vez de causar reboot.
- Documente partições e tamanho real do firmware. Se usar OTA, dimensione dois slots apenas se couberem; não invente suporte OTA sem espaço e caminho de recuperação.

### 15. Sequência de implementação

Trabalhe por marcos executáveis, atualizando o estado e prosseguindo sem pedir confirmação a cada decisão técnica rotineira.

**Marco 0 — descoberta e redução de risco:** examinar repositório/hardware, fixar toolchain/BSP, listar dependências e licenças, testar a compilação mínima e iniciar o port/prova RDP com NLA/TLS. Avaliar imediatamente os bloqueios de USB Ethernet e WireGuard sobre Ethernet.

**Marco 1 — base do aparelho:** display, toque, teclado oficial, microSD, recursos externos, UI inicial e diagnósticos. Gerar firmware compilável.

**Marco 2 — cadastros e segurança:** conexões, identidades, favoritos, cofre, confiança, históricos, migrações e recuperação de escrita interrompida.

**Marco 3 — acesso SSH e arquivos:** shell interativo com teclado real, chave com passphrase, SCP individual e SFTP com navegador e fila.

**Marco 4 — conectividade completa:** Wi-Fi, primeiro driver Ethernet USB e seleção de interface; WireGuard funcional sobre os dois transportes, com vínculo por conexão e bloqueio de fallback indevido.

**Marco 5 — integração RDP:** integrar a prova ao ciclo de sessão, UI, cofre, teclado e rede. Executar a matriz de Windows e otimizar o caminho gráfico e a memória.

**Marco 6 — acabamento e entrega:** navegação, recursos do SD, empacotamento, build reproduzível, documentação e relatório honesto de validação.

A investigação do Marco 0 deve acontecer antes de o projeto investir semanas em funções secundárias. SSH ou telas bonitas não substituem a necessidade de resolver RDP. Se houver bloqueio externo, mantenha-o visível e avance nas partes independentes.

### 16. Testes e critérios de aceite

Use testes automatizados para lógica crítica portável e testes de integração para os protocolos. Mocks servem para desenvolvimento, nunca como evidência de conexão real.

Testes automatizados relevantes:

- Parsing, limites e migração de configurações; referência inválida e arquivo corrompido.
- Cofre: senha errada, adulteração de ciphertext/metadados, nonces, alteração de senha e backup/restauração.
- Known hosts: novo endpoint, porta diferente, chave igual e chave alterada; preservação do histórico após reinício.
- Parser WireGuard, CIDRs, múltiplas rotas, opções não suportadas e prevenção de fallback fora do túnel.
- Caminhos de arquivo e nomes maliciosos, sobrescrita, espaço insuficiente e interrupção.
- Parser UTF-8/ANSI com entradas fragmentadas, limites e dados malformados.
- Eventos de teclado: modificadores, release, foco, acentos e comandos remotos.

Validação em equipamento e rede reais:

1. Inicializar o Tab5 e carregar recursos exclusivamente de `/TabRemoto/`.
2. Cadastrar um favorito, uma identidade e uma chave; reiniciar e comprovar persistência.
3. Desbloquear o cofre e conectar SSH com chave protegida por passphrase.
4. Alterar a chave de host no laboratório e comprovar bloqueio e histórico.
5. Usar o teclado oficial para preencher formulários, escrever comandos, interromper com Ctrl+C e operar uma aplicação de terminal interativa.
6. Enviar e baixar arquivo por SCP e por SFTP, comparando hashes quando disponível; testar cancelamento e limite do cartão.
7. Obter IP e abrir uma conexão por Ethernet USB; manter teclado oficial funcional ao mesmo tempo.
8. Repetir SSH/transferência por WireGuard sobre Wi-Fi e depois sobre Ethernet.
9. Derrubar a VPN e provar que uma conexão marcada como obrigatória não sai diretamente pela interface física, inclusive durante reconexão e resolução DNS.
10. Conectar RDP com NLA/TLS, desenhar desktop e usar teclado e ponteiro no Tab5.
11. Executar os casos de Windows antigos e modernos disponíveis, anotando atualizações, políticas, TLS/CredSSP, autenticação e resultado.
12. Testar SD ausente, somente leitura, removido durante gravação e configuração corrompida; verificar recuperação sem apagar os cadastros válidos.
13. Executar ciclos de conectar/desconectar e uma sessão prolongada, medindo vazamento de memória, travamentos e responsividade.

Entregue uma matriz RDP com linhas para Server 2008, 2008 R2, 2012, 2012 R2, 2016, 2019, 2022, 2025 e versões posteriores efetivamente existentes. Colunas mínimas: sistema/build/patches, tipo de conta, política NLA, TLS/CredSSP negociado, resolução, rede, VPN, resultado e evidência. Marque sem ambiguidade o que não foi testado.

Não classifique o conjunto completo como concluído enquanto RDP, SSH, SCP, Ethernet USB e WireGuard sobre ambas as interfaces não tiverem evidência adequada ao nível declarado. Uma entrega parcial deve listar o que funciona e os bloqueios exatos.

### 17. Entregáveis

Produza os artefatos seguintes, ajustando a nomenclatura ao repositório:

- Código-fonte modular e compilável para o Tab5.
- `CMakeLists.txt`, `sdkconfig.defaults`, manifesto/lock de componentes e tabela de partições compatíveis.
- `README.md` com objetivo, instalação, uso, build, pinagens/fontes consultadas e estado real de cada função.
- `docs/viabilidade.md`, `docs/arquitetura.md`, `docs/seguranca.md`, `docs/armazenamento.md`, `docs/teclado.md`, `docs/usb_ethernet_compatibilidade.md`, `docs/rdp_compatibilidade.md` e `docs/testes.md`.
- `docs/status.md` com funções prontas, compiladas, testadas e bloqueadas, sem misturar os níveis.
- Recursos prontos para copiar em `sdcard/TabRemoto/`, incluindo manifest, fontes, ícones, tema, idioma e configurações de exemplo sem credenciais reais.
- Ferramenta para empacotar firmware e recursos, validar o manifest e montar a pasta de distribuição. Nunca incluir cofre pessoal, chaves ou arquivos de usuário no pacote de release.
- Instruções exatas de flash, endereços/partições e recuperação, correspondentes ao build efetivo.
- Se o build tiver sido executado, gerar `build`/relatório de tamanho e pacote de distribuição com os binários realmente produzidos. Se não tiver sido executado, declarar isso claramente, sem criar um `.bin` fictício.
- Inventário de dependências, licenças e patches aplicados.
- Roteiro curto para o usuário validar no seu Tab5, indicando os modelos/VID-PID de adaptador Ethernet testados e os servidores de laboratório utilizados.

No encerramento, explique de forma objetiva o que foi implementado, o que foi compilado, quais testes passaram, quais exigem o aparelho e quais requisitos continuam pendentes. Preserve o requisito de todos os dados e recursos externos ficarem em `/TabRemoto/`.

### 18. Referências técnicas iniciais

Use as fontes abaixo como ponto de partida e confira seu conteúdo e versões no início da implementação. Elas não provam, sozinhas, que um port funciona no Tab5.

- Hardware Tab5: https://docs.m5stack.com/en/core/Tab5
- Teclado oficial I²C: https://docs.m5stack.com/en/tab5/Tab5_Keyboard
- Demonstração oficial Tab5: https://github.com/m5stack/M5Tab5-UserDemo
- M5Unified: https://github.com/m5stack/M5Unified
- USB Host no ESP32-P4: https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html
- Driver USB CDC-ECM: https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_ecm.html
- FreeRDP: https://github.com/FreeRDP/FreeRDP
- Compilação do FreeRDP: https://github.com/FreeRDP/FreeRDP/wiki/Compilation
- libssh2 e suas APIs: https://libssh2.org/
- libssh: https://www.libssh.org/
- WireGuard para ESP-IDF, candidato que requer inspeção e possíveis adaptações: https://github.com/trombik/esp_wireguard
- WireGuard sobre lwIP, base a avaliar: https://github.com/smartalock/wireguard-lwip
- RDP, especificação MS-RDPBCGR: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/
- CredSSP, comportamento e versões de Windows: https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-cssp/0b458a93-de59-44af-82f1-ebedb585f5c6

**Comece agora pelo repositório, pelo build mínimo do Tab5 e pela redução dos riscos de RDP, USB Ethernet e WireGuard. Avance para implementação real e mantenha os resultados verificáveis.**

## Fim do prompt
