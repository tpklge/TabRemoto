# Segurança — estado atual

A etapa 3 implementa cofre local AES-256-GCM, scrypt, cadastros,
identidades, favoritos e pins explícitos por endpoint. Detalhes e limites em
[cadastros_seguranca.md](cadastros_seguranca.md). Senha mestra com exatamente
8 letras/números, conforme escolha do usuário; nunca uma senha fixa do firmware.
SSH autenticou no Tab5 com senha e chave Ed25519 cifrada por passphrase;
SFTP/SCP fizeram roundtrip real com SHA-256. RDP segue indisponível. WireGuard sobre Wi-Fi passou em tráfego e bloqueio
após parar; Ethernet e full tunnel não foram validados. Nenhuma confiança é aceita automaticamente. Formatos e
limites do backend estão em [rede_ssh.md](rede_ssh.md).

O cliente de manutenção obtém token pela USB e usa a API HTTP autenticada
existente do Launcher na rede local. O token fica apenas na RAM do processo,
não é impresso nem incluído nos relatórios. HTTP não oferece sigilo de TLS;
credenciais de teste seguem por USB com envelope AES-GCM de uso único.
A API HTTP envia firmware/recursos públicos e recupera backup cifrado/local. Uma futura instalação só por USB pode evitar esse transporte.

Backup integral contém dados privados do aparelho: mantê-lo local em
`artifacts/device-backups/` (ignorado pelo Git), fora da distribuição.
Não ativar flash encryption, Secure Boot, eFuses ou atualização do C6 nesta fase.
Não considerar o SD criptografado por existir criptografia de flash no chip.

Manifest/hash protegem integridade acidental; atualização autenticada é pendente.
Não escrever logs de terminal ou credenciais. Não usar senhas de servidores como
senha mestra nem persistir cadastros/cofre em NVS compartilhada do Launcher.
