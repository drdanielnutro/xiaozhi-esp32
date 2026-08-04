#ifndef PV_STRINGS_H
#define PV_STRINGS_H

// Textos pt-BR do Professor Virtual (DOCUMENTACAO-APP.md, Apêndice A).
// Mantido fora do sistema de locales do assistente por decisão estrutural
// (decision-log 2026-08-03): header próprio, sem tocar main/assets/locales.
namespace PvStrings {

inline constexpr const char* kAppName = "Professor Virtual";
inline constexpr const char* kBootStatus = "Iniciando...";
inline constexpr const char* kNetScanning = "Procurando redes Wi-Fi...";
inline constexpr const char* kNetConnecting = "Conectando a ";
inline constexpr const char* kNetConnected = "Conectado a ";
inline constexpr const char* kNetDisconnected = "Sem conexão com a rede";
inline constexpr const char* kNetConfigMode = "Modo de configuração de rede";

// Tela de status de rede.
inline constexpr const char* kNetStartingWifi = "Ligando o Wi-Fi...";
inline constexpr const char* kNetReady = "Tudo pronto para começar!";
inline constexpr const char* kNetWaitingConfig = "Falta configurar o servidor";
inline constexpr const char* kNetConfigModeExit = "Rede escolhida. Conectando...";
inline constexpr const char* kNetDisconnectedHint =
    "Vou tentar de novo sozinho. Se demorar, peça ajuda a um adulto.";

// Instruções do modo de configuração Wi-Fi do WifiBoard. O texto final é
// montado como: prefixo + SSID do ponto de acesso + meio + URL do portal.
inline constexpr const char* kWifiApHintPrefix = "Peça a um adulto:\n1) conecte o celular na rede ";
inline constexpr const char* kWifiApHintMiddle = "\n2) abra o endereço ";
inline constexpr const char* kWifiApHintSuffix = "\n3) escolha o Wi-Fi da casa.";

// Tela de configuração do backend (uso adulto).
inline constexpr const char* kCfgTitle = "Configuração do Professor";
inline constexpr const char* kCfgSubtitle = "Esta tela é para um adulto preencher.";
inline constexpr const char* kCfgUrlLabel = "Endereço do servidor";
inline constexpr const char* kCfgUrlPlaceholder = "https://servidor.exemplo.com";
inline constexpr const char* kCfgTokenLabel = "Token do dispositivo";
inline constexpr const char* kCfgTokenPlaceholderNew = "Digite o token do dispositivo";
inline constexpr const char* kCfgTokenPlaceholderKeep = "Já salvo. Deixe vazio para manter.";
inline constexpr const char* kCfgSaveButton = "Salvar";
inline constexpr const char* kCfgSaving = "Salvando...";
inline constexpr const char* kCfgSaved = "Configuração salva.";
inline constexpr const char* kCfgErrUrlEmpty = "Informe o endereço do servidor.";
inline constexpr const char* kCfgErrTokenEmpty = "Informe o token do dispositivo.";

// Motivos de abertura da tela de configuração. Nunca contêm dado do servidor
// nem qualquer parte do token.
inline constexpr const char* kCfgReasonMissing = "Faltam os dados do servidor para começar.";
inline constexpr const char* kCfgReasonUnauthorized = "Token recusado. Digite o token de novo.";
inline constexpr const char* kCfgReasonUnavailable =
    "O servidor respondeu, mas ainda não tem o token do dispositivo configurado.";
inline constexpr const char* kCfgReasonManual = "Confira os dados do servidor.";

// Indicador de conexão com o backend, presente em todas as telas do PV.
inline constexpr const char* kConnOnline = "servidor: conectado";
inline constexpr const char* kConnOffline = "servidor: desconectado";

// Hidratação (§9.1): health + state + lesson depois que a rede sobe.
inline constexpr const char* kHydrating = "Falando com o professor...";
inline constexpr const char* kHydrateRetryHint = "Vou tentar de novo sozinho em alguns segundos.";
inline constexpr const char* kHydrateErrNetwork = "Não consegui falar com o servidor.";
inline constexpr const char* kHydrateErrHttp = "O servidor respondeu com um erro.";
inline constexpr const char* kHydrateErrParse = "Não entendi a resposta do servidor.";
inline constexpr const char* kHealthLost = "Perdi o contato com o servidor.";

// Telas placeholder das rotas do §9.1. O subtítulo diz, em linguagem simples,
// o que a fase seguinte do firmware vai trazer para aquela tela.
inline constexpr const char* kRoutePreparationTitle = "Vamos preparar a lição";
inline constexpr const char* kRoutePreparationSubtitle =
    "Em breve: fotografar as páginas, revisar e enviar a lição.";
inline constexpr const char* kRouteTutoringTitle = "Hora da lição!";
inline constexpr const char* kRouteTutoringSubtitle =
    "Em breve: gravar a pergunta, mostrar a resposta e ouvir o professor.";
inline constexpr const char* kRouteCelebrationTitle = "Você terminou tudo!";
inline constexpr const char* kRouteCelebrationSubtitle =
    "Em breve: a comemoração completa do fim da lição.";
inline constexpr const char* kRouteFailsafeTitle = "Preciso de um adulto aqui";
inline constexpr const char* kRouteFailsafeBadge = "Atenção";
inline constexpr const char* kRouteFailsafeSubtitle =
    "Em breve: a tela do adulto com senha para destravar a tarefa.";

// Detalhe da tela de tutoria: prova de que o espelho foi hidratado de verdade.
inline constexpr const char* kRouteItemPrefix = "Item atual: ";
inline constexpr const char* kRouteTaskPrefix = "Tarefa atual: ";
inline constexpr const char* kRouteUnknownValue = "(sem tarefa marcada)";

}  // namespace PvStrings

#endif  // PV_STRINGS_H
