#include "config_parser.h"

#include <iostream>
#include <fstream>
#include <queue>
#include <set>
#include <unordered_set>

#include <sys/stat.h>

#include <wblib/wbmqtt.h>
#include <wblib/json_utils.h>

#include "log.h"
#include "murmurhash.h"

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::JSON;

#define LOG(logger) ::logger.Log() << "[config] "

namespace
{
    const std::string SINGLE_POINT_CONFIG_VALUE("single");
    const std::string MEASURED_VALUE_SHORT_CONFIG_VALUE("short");
    const std::string MEASURED_VALUE_SCALED_CONFIG_VALUE("scaled");

    const std::unordered_map<std::string, TIecInformationObjectType> Types = {
            { SINGLE_POINT_CONFIG_VALUE,          SinglePoint },
            { MEASURED_VALUE_SHORT_CONFIG_VALUE,  MeasuredValueShort },
            { MEASURED_VALUE_SCALED_CONFIG_VALUE, MeasuredValueScaled }
        };

    TIecInformationObjectType GetIoType(const std::string& t)
    {
        auto it = Types.find(t);
        if (it == Types.end()) {
            throw std::runtime_error("Unknown information object type:" + t);
        }
        return it->second;
    }

    TControlsConfig LoadControls(const std::string& deviceName, const Json::Value& controls, std::set<uint32_t>& UsedAddresses)
    {
        TControlsConfig res;
        for (const auto& control: controls) {
            bool enabled = false;
            Get(control, "enabled", enabled);
            if (enabled) {
                std::string name(control["name"].asString());
                uint32_t    ioa = control["address"].asUInt();
                if (UsedAddresses.count(ioa)) {
                    LOG(Warn) << "Control '" << name << "' of device '" << deviceName << "' has duplicate address " << ioa;
                } else {
                    UsedAddresses.insert(ioa);
                    res.insert({name, { ioa, GetIoType(control["iec_type"].asString()) }});
                }
            }
        }
        return res;
    }

    TDeviceConfig LoadDevices(const Json::Value& devices, std::set<uint32_t>& UsedAddresses)
    {
        TDeviceConfig res;
        for (const auto& device: devices) {
            bool enabled = false;
            Get(device, "enabled", enabled);
            if (enabled) {
                std::string name(device["name"].asString());
                res[name] = LoadControls(name, device["controls"], UsedAddresses);
            }
        }
        return res;
    }

    TMosquittoMqttConfig LoadMqttConfig(const Json::Value& configRoot)
    {
        TMosquittoMqttConfig cfg;
        if (configRoot.isMember("mqtt")) {
            Get(configRoot["mqtt"], "host", cfg.Host);
            Get(configRoot["mqtt"], "port", cfg.Port);
            Get(configRoot["mqtt"], "keepalive", cfg.Keepalive);
            bool auth = false;
            Get(configRoot["mqtt"], "auth", auth);
            if (auth) {
                Get(configRoot["mqtt"], "username", cfg.User);
                Get(configRoot["mqtt"], "password", cfg.Password);
            }
        }
        return cfg;
    }

    class AddressAssigner
    {
            std::set<uint32_t> UsedAddresses;
        public:
            AddressAssigner(const Json::Value& config)
            {
                for (const auto& device: config["devices"]) {
                    if (device.isMember("controls")) {
                        for (const auto& control: device["controls"]) {
                            UsedAddresses.insert(control["address"].asUInt());
                        }
                    }
                }
            }

            uint32_t GetAddress(const std::string& topicName)
            {
                uint32_t newAddr = MurmurHash2A((const uint8_t*)topicName.data(), topicName.size(), 0xA30AA568) & 0xFFFFFF;
                const uint32_t ADDR_SALT = 7079;
                while (UsedAddresses.count(newAddr)) {
                    newAddr = (newAddr + ADDR_SALT) & 0xFFFFFF;
                }
                UsedAddresses.insert(newAddr);
                return newAddr;
            }
    };

    bool IsConvertibleControl(PControl control)
    {
        return (control->GetType() != "text" && control->GetType() != "rgb");
    }

    Json::Value MakeControlConfig(const std::string& name, const std::string& info, uint32_t addr, const std::string& iecType)
    {
        Json::Value cnt(Json::objectValue);
        cnt["name"] = name;
        cnt["info"] = info;
        cnt["enabled"] = false;
        cnt["address"] = addr;
        cnt["iec_type"] = iecType;
        return cnt;
    }

    void AppenControl(Json::Value& root, PControl c, AddressAssigner& aa)
    {
        if (!IsConvertibleControl(c)) {
            ::Warn.Log() << "'" << c->GetId()
                            << "' of type '" << c->GetType()
                            << "' from device '" << c->GetDevice()->GetId()
                            << "' is not convertible to IEC 608760-5-104 information object";
            return;
        }

        std::string info(c->GetType());
        info += (c->IsReadonly() ? " (read only)" : " (setup is allowed)");

        std::string deviceControlPair(c->GetDevice()->GetId() + "/" + c->GetId());

        if (c->GetType() == "switch" || c->GetType() == "pushbutton") {
            root.append(MakeControlConfig(c->GetId(), info, aa.GetAddress(deviceControlPair), SINGLE_POINT_CONFIG_VALUE));
            return;
        }
        root.append(MakeControlConfig(c->GetId(), info, aa.GetAddress(deviceControlPair), MEASURED_VALUE_SHORT_CONFIG_VALUE));
    }

    void UpdateDeviceConfig(Json::Value& deviceConfig, PDevice device, AddressAssigner& addressAssigner)
    {
        std::unordered_set<std::string> oldControls;
        for (auto& control: deviceConfig["controls"]) {
            oldControls.insert(control["name"].asString());
        }

        for (auto control: device->ControlsList()) {
            if (!oldControls.count(control->GetId())) {
                AppenControl(deviceConfig["controls"], control, addressAssigner);
            }
        }
    }

    Json::Value MakeDeviceConfig(PDevice device, AddressAssigner& addressAssigner)
    {
        Json::Value dev(Json::objectValue);
        dev["name"] = device->GetId();
        dev["enabled"] = false;
        dev["controls"] = Json::Value(Json::arrayValue);
        UpdateDeviceConfig(dev, device, addressAssigner);
        return dev;
    }
}

TConfig LoadConfig(const std::string& configFileName, const std::string& configSchemaFileName)
{
    auto config = JSON::Parse(configFileName);
    JSON::Validate(config, JSON::Parse(configSchemaFileName));
    std::set<uint32_t> usedAddresses;

    TConfig cfg;
    cfg.Iec.BindIp        = config["iec"]["host"].asString();
    cfg.Iec.BindPort      = config["iec"]["port"].asUInt();
    cfg.Iec.CommonAddress = config["iec"]["address"].asUInt();
    cfg.Mqtt              = LoadMqttConfig(config);
    cfg.Devices           = LoadDevices(config["devices"], usedAddresses);
    Get(config, "debug", cfg.Debug);
    return cfg;
}

void UpdateConfig(const string& configFileName, const string& configSchemaFileName)
{
    auto config = JSON::Parse(configFileName);
    JSON::Validate(config, JSON::Parse(configSchemaFileName));

    WBMQTT::TMosquittoMqttConfig mqttConfig(LoadMqttConfig(config));
    auto mqtt = NewMosquittoMqttClient(mqttConfig);
    auto backend = NewDriverBackend(mqtt);
    auto driver = NewDriver(TDriverArgs{}
        .SetId("wb-mqtt-iec104-config_generator")
        .SetBackend(backend)
    );
    driver->StartLoop();
    UpdateConfig(driver, config);
    driver->StopLoop();

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::ofstream file(configFileName);
    writer->write(config, &file);
    file << std::endl;
}

void UpdateConfig(PDeviceDriver driver, Json::Value& oldConfig)
{
    driver->WaitForReady();
    driver->SetFilter(GetAllDevicesFilter());
    driver->WaitForReady();

    AddressAssigner addressAssigner(oldConfig);

    std::map<std::string, PDevice> mqttDevices;
    auto tx = driver->BeginTx();
    for (auto& device: tx->GetDevicesList()) {
        mqttDevices[device->GetId()] = device;
    }

    for (auto& oldDevice: oldConfig["devices"]) {
        auto mqttDevice = mqttDevices.find(oldDevice["name"].asString());
        if (mqttDevice != mqttDevices.end()) {
            UpdateDeviceConfig(oldDevice, mqttDevice->second, addressAssigner);
            mqttDevices.erase(mqttDevice);
        }
    }

    for (auto& mqttDevice: mqttDevices) {
        auto deviceConfig = MakeDeviceConfig(mqttDevice.second, addressAssigner);
        if (deviceConfig["controls"].size()) {
            oldConfig["devices"].append(deviceConfig);
        }
    }
}