# TabRemoto

Leia `TabRemoto_prompt_completo.md`, `docs/status.md` e `docs/testes.md` antes de continuar.
Trabalhe por etapa executável e só encerre depois de executar os testes possíveis
da etapa. Registre falhas e limites sem declarar funcionalidades por mock/UI.

O usuário autorizou compilar, enviar arquivos, instalar e executar automaticamente
no Tab5 real por USB/LauncherdoPaludo. Não peça confirmação para essas operações
rotineiras. Identifique o dispositivo por VID/PID: USB BillBoard não é o Tab5.
Use `tools/launcher.py` e a API do Launcher para instalação de app-only; preserve
outros apps. Qualquer ajuste de slots passa pelo plano e backup verificado do
Launcher. Nunca grave bootloader/tabela diretamente, apague a flash, programe o C6
automaticamente ou queime eFuses. Não exponha tokens de manutenção nem inclua
backups da flash ou dados pessoais no pacote público.

Build: `bash tools/build.sh build`. Ferramentas Python devem usar o ambiente IDF
indicado em `tools/build.sh`. Dados e recursos externos somente em `/TabRemoto/`
no SD. O diagnóstico interno mínimo é permitido quando SD/recursos falharem.

Marcos completos do produto ainda dependem de SSH/SCP/SFTP, Ethernet, WireGuard
em ambos os transportes e RDP NLA/TLS validados com servidores reais.
