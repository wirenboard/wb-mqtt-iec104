#include "gateway.h"
#include <wblib/json/json.h>

struct TConfig
{
    IEC104::TServerConfig Iec;
    WBMQTT::TMosquittoMqttConfig Mqtt;
    TDeviceConfig Devices;
    bool Debug = false;
};

TConfig LoadConfig(const std::string& configFileName, const std::string& configSchemaFileName);

/**
 * @brief Updates config.
 *        Takes MQTT brocker params from old config and creates new instance of device driver.
 *        Writes resulting config instead of old one.
 *
 * @param configFileName full path and file name of config to update
 * @param configSchemaFileName full path and file name of config's JSON schema
 */
void UpdateConfig(const std::string& configFileName, const std::string& configSchemaFileName);

/**
 * @brief Updates oldConfig with new controls from driver.
 *
 * @param driver active instance of device driver
 * @param oldConfig JSON object with config to update
 */
void UpdateConfig(WBMQTT::PDeviceDriver driver, Json::Value& oldConfig);
