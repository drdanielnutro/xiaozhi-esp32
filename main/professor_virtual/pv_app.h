#ifndef PV_APP_H
#define PV_APP_H

// Aplicativo Professor Virtual: substitui o assistente XiaoZhi quando
// CONFIG_PROFESSOR_VIRTUAL=y. O Application do assistente continua compilado,
// mas nunca é inicializado (decisão Q1a do decision-log).
class PvApp {
public:
    static PvApp& GetInstance() {
        static PvApp instance;
        return instance;
    }

    void Initialize();
    [[noreturn]] void Run();

private:
    PvApp() = default;
    PvApp(const PvApp&) = delete;
    PvApp& operator=(const PvApp&) = delete;

    void SetupBootScreen();
};

#endif  // PV_APP_H
