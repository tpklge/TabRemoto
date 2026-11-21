# Armazenamento — etapa inicial

`sd_root` em `components/tabremoto_storage/storage.hpp` traduz a raiz lógica
`/TabRemoto/` para `/sdcard/TabRemoto/`. Todos os recursos/diagnósticos do app
ficam abaixo dela. O Launcher conserva seus próprios backups em sua pasta.

O boot monta FAT pelo BSP, sem formatar em caso de erro, e cria apenas diretórios
ausentes. O teste usa `mkstemp`, grava um payload conhecido, faz fsync, relê,
compara, fecha e remove só o temporário criado. Arquivos existentes não são
truncados pelo probe. Um SD sem escrita não é apresentado como persistente.

O manifest é limitado a 2 KiB, schema/app/compatibilidade exatos, e usa allowlist
de três arquivos públicos com limite de 16 KiB cada e SHA-256. Caminhos vindos
do manifest não são usados para abrir arquivos arbitrários. Erro deixa disponível
o diagnóstico interno. Hash detecta corrupção, não autentica um pacote.

Tema/localização/app.json são recursos iniciais; o diagnóstico usa estilos e
fontes internos mínimos. Fontes adicionais e UI completa serão carregadas do SD
no Marco 1. O instalador preserva configuração divergente, recusando sobrescrita.

Ainda pendentes: JSON transacional com recuperação/backup, migrações, detecção
de remoção e montagem novamente, limites/rotação de históricos e logs,
configurações completas e política de sessões durante perda do cartão.
FAT/fsync não garantem atomicidade contra queda de energia.

O empacotador usa allowlist de recursos públicos; não varre o SD, vault, history,
backups ou downloads. A distribuição contém somente binário app-only real,
recursos públicos e lock. Não distribuir `artifacts/device-backups/`.

## Atualização da base

O manifest agora valida cinco arquivos (tema, locale, configuração pública,
fonte e licença OFL). Limite de cada recurso: 256 KiB. Tema/locale têm leitura
JSON limitada a 4 KiB; strings da UI até 160 bytes. Fonte é mantida em RAM.
Montagem/retry/probes/captura são serializados no worker/console, nunca em
callback de botão. Estado publicado em RAM e atualização dos recursos no
contexto LVGL. Ver base_aparelho.md para política de remoção e ensaios.

## Cadastros e cofre — etapa 3

Conexões, identidades, segredos e confiança são um documento autenticado e
criptografado em duas gerações em vault/store.a e store.b. Os metadados não
são gravados em texto puro por padrão. Exportação pública é explícita em
config/connections-gN.json; backup criptografado distinto em backups/vault-gN.trv.
O worker usa o mesmo mutex de I/O do SD para impedir remount durante operações.
Leia cadastros_seguranca.md para formato, limites e recuperação explícita.
