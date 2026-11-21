# Interface inicial e cadastros

O desenho segue o LauncherdoPaludo: fundo grafite, navegação lateral,
destaques vermelhos, cartões com cantos arredondados e títulos sem serifa.
A tela inicial reúne SSH, RDP, cadastros e rede em quatro cartões com ícones,
descrições curtas, favoritos e uma faixa de estado.

Nos cadastros, as páginas ficam na lateral e cada campo tem sua própria legenda.
Conexões, identidades e credenciais têm uma lista à esquerda e um formulário à
direita. O registro carregado fica destacado. Opções como protocolo, identidade,
autenticação e VPN abrem uma lista de seleção com cancelamento, em vez de
alternar silenciosamente a cada toque. Salvar recebe destaque; excluir mantém
confirmação. O cofre bloqueado apresenta um acesso direto ao desbloqueio.

A senha mestra continua alfanumérica, com oito caracteres. Senhas e chaves
permanecem mascaradas. Formato, criptografia e operações dos cadastros são os
mesmos. A reformulação não altera a área remota do RDP.

## Verificação

`tools/check_launcher_ui.py` usa a candidata com diagnósticos para percorrer as
páginas reais do LVGL, verificar limites dos textos, selecionar/cancelar opções,
exercitar foco e obter capturas da renderização no aparelho. A prévia mantém o
cofre pessoal bloqueado e rejeita gravações. As capturas ficam privadas, fora do
pacote. Isso não substitui a conferência de registros pessoais após desbloquear
normalmente na tela.

A versão de uso foi compilada e validada com diagnósticos desativados, sem as ações
cifradas `security-layout` e `ui-capture`. A instalação usa o Launcher com backup
e comparação do cofre e das imagens dos outros aplicativos.

Os ensaios passaram no Tab5, com 14 capturas sem reiniciar. A leitura integral
confirmou a imagem instalada, o cofre pessoal e os demais aplicativos. A versao
sem diagnosticos passou Wi-Fi automatico, SSH e RDP com servidores reais,
rejeicao de pin divergente e 30 segundos estaveis; terminou na tela inicial.

A captura cifrada encontrou inicialmente estouro da pilha USB: o buffer de
linha saiu da pilha e os dois caminhos de captura passaram novamente. A coleta
USB tambem ganhou protecao contra respostas atrasadas no desafio de
provisionamento, recibo e resultado de acao; 27 testes Python passaram.

Evidencias: `artifacts/launcher-ui-device.json`,
`artifacts/launcher-ui-production-device.json`,
`artifacts/launcher-ui-final-preservation.json` e
`artifacts/launcher-ui-final-home.json`. Imagens e backups ficam privados.
