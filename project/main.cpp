#include "metrics.h"
#include <thread>

// Блокирует выполнение, пока не нажат Enter
void waitForExitKey() {
    std::cout << "Press Enter to stop the program..." << std::endl;
    std::cin.get(); 
}

// Метрика загрузки cpu
double cpuLoad()
{
    int cores_count = std::thread::hardware_concurrency();
    return std::round(static_cast<double>(rand()) / RAND_MAX * cores_count * 100.0) / 100.0;
}

// Метрика статуса сервера (string)
std::string getRandomServerStatus() {
    const char* statuses[] = { "OK", "WARNING", "ERROR", "RECOVERING" };
    return statuses[rand() % 4];
}

// Метрика времени ответа (double 0.1-500.0 ms)
double getRandomResponseTime() {
    return 0.1 + static_cast<double>(rand()) / (RAND_MAX / 499.9);
}

int main() {
    AsyncMetricsWriter metricsWriter("metrics.log", 500);

    std::atomic<bool> stopFlag{ false };

    // Поток для обработки клавиш
    std::thread keyThread([&stopFlag, &metricsWriter]() {
        std::cout << "Press Enter to exit..." << std::endl;
        while (!stopFlag) {
            char input = std::cin.get();
            if (input == '\n') {
                stopFlag = true;
                break;
            }
        }
        });

    while (!stopFlag)
    {
        auto a = cpuLoad();
        int httpRequests = rand() % 101;
        std::string status = getRandomServerStatus();
        double resp = getRandomResponseTime();

        metricsWriter.record("CPU", a);
        metricsWriter.record("HTTP requests RPS", httpRequests);
        metricsWriter.record("Server status", status);
        metricsWriter.record("Response time ms", resp);

        //std::cout << a << " " << httpRequests << " " << status << " " << resp<< '\n';

        // Имитация работы системы (метрики приходят в рандомные моменты)
        std::this_thread::sleep_for(std::chrono::milliseconds(200 + rand() % 800));
    }

    keyThread.join();
    return 0;
}