# Próximas cinco etapas após esta entrega

1. **Ethernet física.** Disponibilizar conexão USB Host para o Baseus AX88179A;
   ensaiar DHCP, IP fixo, hotplug, WireGuard e RDP. Somente USB-C Device está
   disponível hoje; driver compilado não comprova esses cenários.
2. **Dois RDP independentes.** Validar duas contas/sessões autorizadas simultâneas
   e ampliar a matriz Windows quando houver outros servidores autorizados.
   No momento, somente 192.168.88.102 está autorizado e disponível.
3. **Estabilidade prolongada.** Ensaio de 24 horas, várias quedas físicas de
   Wi-Fi, repetidos ciclos de sessões e acompanhamento de memória/latência.
   A entrega passou dez minutos com carga e mais dois em produção.
4. **Falhas físicas de armazenamento/energia.** Retirada do SD, cartão somente
   leitura e corte de energia durante escrita de cofre/backup, usando dados
   descartáveis. Remontagens lógicas e corrupção autenticada já passaram.
5. **IPv6 externo e quatro túneis.** Ensaiar transporte/endpoint físico IPv6 e
   quatro peers simultâneos quando houver rede apropriada. IPv6 dentro de dois
   túneis com DNS/rotas sobrepostas já passou sobre Wi-Fi IPv4.

Resultado das cinco etapas implementadas: [etapas_finais.md](etapas_finais.md).
