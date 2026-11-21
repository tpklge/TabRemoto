# RDP — servidor validado e limites da matriz

OpenSSL 3.5.9 e FreeRDP/WinPR 3.25.0 completos executam no P4. A candidata
cc39a557bc5c passou autenticação NLA/TLS, pin SHA-256 autorizado, desktop real,
eventos de teclado/mouse e desconexão no servidor 192.168.88.102. A captura
privada foi inspecionada: Windows Server 2022 Standard Evaluation, build
20348.fe_release.210507-1500 (identificação visível no desktop, não consulta de
patches instalados). Sem gateway externo. Testes atuais em Wi-Fi direto e WireGuard IPv4/IPv6 interno, com bitmap16.
Clipboard Unicode, resoluções 1280x720/960x540/800x450, quatro reconexões após
quedas separadas e coexistência com quatro SSH passaram. Dez minutos sob
carga e dois na versão de uso não substituem 24 h. Crescimento depende de
blocos de memória e é recusado com aviso sem perder a sessão. Ethernet,
IPv6 físico e dois RDP independentes ainda não testados. Todas as sessões
Windows apontaram somente para .102; ver etapas_finais.md.

| Sistema | Build/patches | Conta | NLA | TLS/CredSSP | Resolução | Rede | VPN | Resultado/evidência |
|---|---|---|---|---|---|---|---|---|
| Server 2008 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2008 R2 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2012 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2012 R2 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2016 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2019 | Não informado | — | — | — | — | — | — | Não testado |
| Server 2022 Standard Evaluation | 20348.fe_release.210507-1500, patches não consultados | Laboratório | Sim, protocolo 2 | TLS/CredSSP nativos | 1280x720/960x540/800x450, bitmap16 | Wi-Fi | WG IPv4/IPv6 interno | Desktop, clipboard, resize, quatro reconexões e carga no Tab5 |
| Server 2025 | Não informado | — | — | — | — | — | — | Não testado |

Adicionar versões posteriores somente depois de confirmar que existem e com
identificação efetiva do laboratório. Não desativar NLA globalmente, negociar
downgrade oculto ou usar gateway externo como substituto do requisito.
