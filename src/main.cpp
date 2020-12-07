#include <getopt.h>

#include <wblib/signal_handling.h>
#include <wblib/wbmqtt.h>

#include "log.h"
#include "config_parser.h"

#define LOG(logger) ::logger.Log() << "[main] "

#define STR(x) #x
#define XSTR(x) STR(x)

using namespace std;
using namespace WBMQTT;

const auto APP_NAME                             = "wb-mqtt-iec104";
const auto CONFIG_FULL_FILE_PATH                = "/etc/wb-mqtt-iec104.conf";
const auto CONFIG_JSON_SCHEMA_FULL_FILE_PATH    = "/usr/share/wb-mqtt-confed/schemas/wb-mqtt-iec104.schema.json";

const auto DRIVER_STOP_TIMEOUT_S = chrono::seconds(10);

//! Maximun time to start application. Exceded timeout will case application termination.
const auto DRIVER_INIT_TIMEOUT_S = chrono::seconds(60);

namespace
{
    void PrintStartupInfo()
    {
        cout << APP_NAME << " " << XSTR(WBMQTT_VERSION) 
             << " git " << XSTR(WBMQTT_COMMIT) 
             << " build on " << __DATE__ << " " << __TIME__ << endl;
    }

    void PrintUsage()
    {
        PrintStartupInfo();
        cout << "Usage:" << endl
             << " " << APP_NAME << " [options]" << endl
             << "Options:" << endl
             << "  -d  level    enable debuging output:" << endl
             << "                 1 - " << APP_NAME << " only;" << endl
             << "                 2 - MQTT only;" << endl
             << "                 3 - both;" << endl
             << "                 negative values - silent mode (-1, -2, -3))" << endl
             << "  -c  config   config file (default /etc/wb-mqtt-iec104.conf)" << endl
             << "  -g  config   update config file with information about active MQTT publications" << endl
    }

    void ParseCommadLine(int     argc,
                         char*   argv[],
                         string& configFile)
    {
        int debugLevel = 0;
        int c;

        while ((c = getopt(argc, argv, "d:c:g:")) != -1) {
            switch (c) {
            case 'd':
                debugLevel = stoi(optarg);
                break;
            case 'c':
                configFile = optarg;
                break;
            case 'g':
                try {
                    UpdateConfig(optarg, CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
                } catch (const exception& e) {
                    std::cerr << "FATAL: " << e.what();
                    exit(1);
                }
                exit(0);
            default:
                PrintUsage();
                exit(2);
            }
        }

        switch (debugLevel) {
        case 0:
            break;
        case -1:
            ::Info.SetEnabled(false);
            break;
        case -2:
            WBMQTT::Info.SetEnabled(false);
            break;
        case -3:
            WBMQTT::Info.SetEnabled(false);
            ::Info.SetEnabled(false);
            break;
        case 1:
            ::Debug.SetEnabled(true);
            break;
        case 2:
            WBMQTT::Debug.SetEnabled(true);
            break;
        case 3:
            WBMQTT::Debug.SetEnabled(true);
            ::Debug.SetEnabled(true);
            break;
        default:
            cout << "Invalid -d parameter value " << debugLevel << endl;
            PrintUsage();
            exit(2);
        }
    }
}

int main(int argc, char *argv[])
{
    string configFile(CONFIG_FULL_FILE_PATH);

    TPromise<void> initialized;
    SignalHandling::Handle({ SIGINT, SIGTERM });
    SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{ SignalHandling::Stop(); });
    SetThreadName(APP_NAME);

    ParseCommadLine(argc, argv, configFile);

    PrintStartupInfo();

    SignalHandling::SetWaitFor(DRIVER_INIT_TIMEOUT_S, initialized.GetFuture(), [&] {
        LOG(Error) << "Driver takes too long to initialize. Exiting.";
        exit(1);
    });

    SignalHandling::SetOnTimeout(DRIVER_STOP_TIMEOUT_S, [&]{
        LOG(Error) << "Driver takes too long to stop. Exiting.";
        exit(1);
    });

    try {
        TConfig config(LoadConfig(configFile, CONFIG_JSON_SCHEMA_FULL_FILE_PATH));
        if (config.Debug) {
            ::Debug.SetEnabled(true);
        }

        SignalHandling::Start();

        auto mqtt    = NewMosquittoMqttClient(config.Mqtt);
        auto backend = NewDriverBackend(mqtt);
        auto driver  = NewDriver(TDriverArgs{}.SetId(APP_NAME).SetBackend(backend));

        driver->StartLoop();
        driver->WaitForReady();

        auto IecServer(IEC104::MakeServer(config.Iec));

        TGateway gateway(driver, IecServer.get(), config.Devices);

        SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] { gateway.Stop(); });

        initialized.Complete();
        SignalHandling::Wait();

    } catch (const exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }

    return 0;
}
