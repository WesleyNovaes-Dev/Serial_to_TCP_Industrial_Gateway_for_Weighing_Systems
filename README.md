# 🚀 Gateway IoT Industrial – Integração de Balanças RS-232 com Redes TCP/IP  
### **Projeto Empresarial – Indústria 4.0 (NCH Brasil)**  
**Desenvolvido por:** *Wesley Davi Zanon Novaes*  

![Badge ESP32](https://img.shields.io/badge/Hardware-ESP32-red)
![Badge NodeJS](https://img.shields.io/badge/Backend-Node.js-green)
![Badge Protocol](https://img.shields.io/badge/Protocol-TCP%2FIP-blue)
![Badge Industry](https://img.shields.io/badge/Indústria-4.0-orange)

---

## 🌐 Visão Geral

Este repositório apresenta um **Gateway IoT Industrial** projetado para **modernizar balanças legadas** (RS-232 e USB) e conectá-las diretamente a redes corporativas para uso em sistemas ERP, MES, supervisórios e aplicações de automação industrial.

Diferente de um TCC acadêmico, este projeto é **uma evolução real** implantada e utilizada na **NCH Brasil**, rodando diariamente em ambiente de produção — com integração já validada com o ERP atual da empresa.

O objetivo central é simples e poderoso:

### **Transformar qualquer balança serial em um dispositivo inteligente totalmente integrado à Indústria 4.0.**

Sem alterar o hardware da balança.  
Sem custos abusivos de modernização.  
Sem soluções proprietárias fechadas.

---

## 🎯 Principais Benefícios

✔ **Elimina apontamentos manuais** de pesagem  
✔ **Reduz erros operacionais**  
✔ **Transmite dados em tempo real** via TCP/IP  
✔ **Suporte a Wi-Fi corporativo, incluindo WPA2-Enterprise (802.1X)**  
✔ **Interface Web moderna** para configuração completa do dispositivo  
✔ **Compatível com ERPs, bancos de dados, supervisórios e APIs**  
✔ **Plug-and-Play** (conecta → configura → integra)

---

## 🧩 Arquitetura Geral

A solução é composta por dois pilares principais:

### **1) Gateway IoT (ESP32)**
Responsável por:
- Receber dados via **RS-232 (UART + MAX3232)** ou USB
- Limpar, validar e tratar frames recebidos
- Detectar mudanças de peso (filtro inteligente)
- Transmitir dados via:
  - **TCP (porta configurável)**
  - **Servidor de Log**
- Servir uma **interface web avançada responsiva**
- Oferecer:
  - Configuração Wi-Fi
  - Configuração Ethernet (quando disponível)
  - IP estático/DHCP
  - Hostname
  - Baud Rate
  - Formato JSON / Texto puro
  - Múltiplos clientes simultâneos

### **2) Backend Integrado (Node.js)**
- Recebe e processa dados enviados pelo gateway
- Permite integração com:
  - ERPs
  - Sistemas legados
  - Bancos de dados
  - Dashboards e BI
- Registro de eventos e logs

---

## 🛠️ Hardware Utilizado

| Componente | Função |
|-----------|--------|
| **ESP32 DevKitC V4** | Core de processamento e Wi-Fi |
| **MAX3232** | Conversão RS-232 → TTL |
| **DB9** | Interface com balanças industriais |
| **Fonte 5V** | Alimentação |
| **Protótipo final** | Pronto para migração a PCB industrial |

---

## 🔌 Conexões (Pinout)

## 🔌 Conexões (Pinout Completo – ESP32 + MAX3232 + W5500)

| **ESP32**           | **Dispositivo** | **Pino no Dispositivo** | **Função** |
|---------------------|------------------|---------------------------|------------|
| **GPIO 16 (RX2)**   | MAX3232          | TX                        | Recepção da Balança (RS-232 → TTL) |
| **GPIO 17 (TX2)**   | MAX3232          | RX                        | Envio para Balança (TTL → RS-232) |
| **GND**             | MAX3232          | GND                       | Referência elétrica |
| **3.3V**            | MAX3232          | VCC                       | Alimentação do conversor |
| **GPIO 18 (SCK)**   | W5500            | SCLK                      | Clock SPI |
| **GPIO 19 (MISO)**  | W5500            | MISO                      | Dados do W5500 → ESP32 |
| **GPIO 23 (MOSI)**  | W5500            | MOSI                      | Dados do ESP32 → W5500 |
| **GPIO 5 (CS)**     | W5500            | CS                        | Chip Select do módulo Ethernet |
| **GPIO 4 (RST)**    | W5500            | RST                       | Reset do módulo Ethernet |
| **GND**             | W5500            | GND                       | Referência elétrica |
| **3.3V ou 5V***     | W5500            | VCC                       | Alimentação (depende do módulo)** |
| **GPIO 2**          | LED Indicador    | LED                       | LED de status do sistema |

> **\*** A maioria dos módulos W5500 funciona com alimentação 3.3V lógica, porém muitos incluem regulador interno e aceitam 5V — verificar o modelo utilizado.

---

## 🧠 Funcionamento do Firmware

1. **Inicialização**  
   - Carrega credenciais
   - Tenta reconectar à última rede
   - Caso falhe → inicia **Modo AP de Configuração**

2. **Leitura da Balança (RS-232)**  
   - Captura bytes brutos
   - Limpa caracteres inválidos
   - Normaliza a string de peso
   - Filtra repetições para reduzir tráfego

3. **Transmissão dos Dados**  
   - Via Socket TCP (porta configurável)
   - Suporte a múltiplos clientes
   - Mecanismos:
     - Retry
     - Reconexão automática
     - Heartbeat de latência

4. **Webserver Interno**  
   - Configuração completa da rede
   - Exibição de status em tempo real
   - Dashboard de balance connection
   - Opções avançadas:
     - IP estático
     - WINS
     - DNS configurável
     - SoftAP customizável

---

## 📡 Integração com ERP (CONFIRMADA)

Este projeto já está homologado com o **ERP utilizado pela NCH Brasil**.

Isso inclui:
- Recebimento automático das pesagens
- Inserção de dados em rotinas internas
- Substituição de processos manuais
- Automação completa do fluxo de entrada de dados

---

## 📸 Screenshots (Inserir após subir as imagens)

> **Coloque as imagens no repositório e substitua pelos links:**  

