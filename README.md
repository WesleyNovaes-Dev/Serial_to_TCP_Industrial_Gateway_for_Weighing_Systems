# 🚀 **Gateway IoT: Modernização de Balanças para Indústria 4.0**

[![GitHub License](https://img.shields.io/github/license/seu-usuario/seu-repositorio?style=flat-square&color=blue)](https://github.com/seu-usuario/seu-repositorio/blob/main/LICENSE)
![Status](https://img.shields.io/badge/Status-Produção-success?style=flat-square)
![Hardware](https://img.shields.io/badge/Hardware-ESP32-critical?style=flat-square&logo=espressif)
![Backend](https://img.shields.io/badge/Backend-Node.js-success?style=flat-square&logo=nodedotjs)

> **Desenvolvedor:** Wesley Davi Zanon Novaes

---

## 🎯 **Visão Geral da Solução**

Este projeto é uma solução de **IoT Industrial (IIoT)** madura e em produção, projetada para integrar balanças industriais legadas (com interfaces RS-232/USB) diretamente à infraestrutura de rede corporativa e aos sistemas de gestão (ERP).

Nascido de um trabalho acadêmico e evoluído para uma aplicação empresarial robusta, este **Gateway IoT** elimina a necessidade de apontamentos manuais de peso, automatizando a coleta de dados no chão de fábrica e viabilizando a digitalização de processos críticos de pesagem.

✅ **Aplicação Real:** O sistema está implantado e integrado com sucesso ao ERP da **NCH Brasil**, demonstrando sua eficácia e confiabilidade em um ambiente industrial exigente.

---

## ✨ **Diferenciais Competitivos e Funcionalidades**

* **Modernização sem Substituição:** Conecta equipamentos legados à Indústria 4.0 sem o alto custo de aquisição de novas balanças com conectividade nativa.
* **Conectividade Corporativa Robusta:** Suporte total a redes Wi-Fi empresariais, incluindo autenticação **WPA2-Enterprise**, essencial para ambientes corporativos seguros.
* **Configuração Simplificada:** Interface Web embarcada (modo Access Point) para comissionamento e configuração rápida dos parâmetros de rede e operação, sem necessidade de cabos ou softwares adicionais.
* **Eficiência de Dados:** Algoritmo proprietário *LineChangeDetector* que filtra leituras redundantes, reduzindo o tráfego de rede em até **90%** e otimizando o armazenamento de dados.
* **Alta Disponibilidade:** Mecanismos automáticos de reconexão e tratamento de falhas de rede, garantindo a continuidade da operação e a integridade dos dados coletados.
* **Integração Flexível:** Transmissão de dados via TCP/IP em formatos padronizados, permitindo fácil integração com diversos backends, middlewares e ERPs de mercado.

---

## 🛠️ **Arquitetura da Solução**

A solução é composta por um hardware robusto e de baixo custo, validado em ambiente industrial.



[Image of Industrial IoT Gateway Architecture Diagram]


| Componente | Função na Solução |
| :--- | :--- |
| **ESP32 (Gateway)** | Núcleo de processamento, inteligência na borda (edge computing) e conectividade Wi-Fi segura. |
| **Interface Serial Industrial** | Módulo conversor (ex: MAX3232) para comunicação confiável com a interface RS-232 da balança. |
| **Fonte de Alimentação Industrial** | Fonte de alimentação estável e adequada para o ambiente de chão de fábrica. |
| **Conectividade Física** | Conector DB9 padrão para interface direta com a balança. |

---

## ⚙️ **Implantação e Operação**

A implantação do Gateway IoT é projetada para ser rápida e minimamente intrusiva.

1.  **Instalação Física:** O hardware do gateway é conectado à porta serial da balança e à alimentação elétrica.
2.  **Configuração de Rede:** Um técnico se conecta à rede Wi-Fi de configuração do gateway e, através de uma interface web amigável, insere as credenciais da rede corporativa (SSID, senha/usuário) e define o modo de IP (DHCP ou Fixo).
3.  **Integração de Backend:** O sistema de backend (ex: Node.js) é configurado para receber os dados transmitidos pelo gateway na porta e IP definidos, processá-los e inseri-los no banco de dados do ERP.
4.  **Operação Automática:** Uma vez configurado, o gateway opera de forma autônoma, capturando, filtrando e transmitindo os dados de pesagem sempre que houver uma nova leitura válida.

---

## 📊 **Resultados de Negócio**

A implantação desta solução na NCH Brasil gerou resultados tangíveis:

* **Automação do Processo:** Eliminação de 100% dos apontamentos manuais de peso nas estações integradas.
* **Redução de Erros:** Mitigação de erros humanos de digitação, aumentando a confiabilidade dos dados de estoque e produção.
* **Dados em Tempo Real:** Disponibilização imediata dos dados de pesagem no ERP para planejamento e controle da produção.
* **ROI Acelerado:** O custo da solução é uma fração do valor de balanças novas com conectividade equivalente, proporcionando um retorno sobre o investimento extremamente rápido.

---

## 🌐 **Contato e Mais Informações**

Para detalhes técnicos, demonstrações ou informações sobre como implementar esta solução em sua empresa, entre em contato.

👉 **[Link para o Site/Portfólio do Desenvolvedor](SEU_LINK_AQUI)** 👈

Este projeto é um exemplo prático de como a tecnologia IoT pode gerar valor real e imediato para a indústria.

---

**Apoio:**
NCH Brasil. Av. Darcí Carvalho Dafferner, 200 - Boa Vista, Sorocaba - SP, 18085-850.

---

**Desenvolvido por Wesley Davi Zanon Novaes.**

---
