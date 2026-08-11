#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_PV_UVC_SPIKE
#include "pv_uvc_spike.h"
#elif CONFIG_PROFESSOR_VIRTUAL
#include "pv_app.h"
#else
#include "application.h"
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
#if CONFIG_PV_UVC_SPIKE
    // Spike de bancada da F2B: substitui o app inteiro (ver pv_uvc_spike.h). A
    // task do spike assume daqui em diante e nunca volta; este app_main retorna
    // e a main task se auto-deleta. Como Board::GetInstance() é lazy, o board —
    // e com ele o CSI, a tela e o áudio — nunca é construído: tela apagada é o
    // esperado. Se a task não nascer, nada mais roda: cair no app normal
    // derrubaria justamente o isolamento que o gate existe para garantir.
    if (!PvUvcSpike::Start()) {
        ESP_LOGE(TAG, "PV-UVC-SPIKE nao pode iniciar; nada mais roda nesta build");
    }
#else
#if CONFIG_PROFESSOR_VIRTUAL
    auto& app = PvApp::GetInstance();
#else
    auto& app = Application::GetInstance();
#endif
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
#endif  // CONFIG_PV_UVC_SPIKE
}
