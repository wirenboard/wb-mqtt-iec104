#include "config_parser.h"

#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <unordered_set>

#include <sys/stat.h>

#include <wblib/json_utils.h>
#include <wblib/wbmqtt.h>

#include "iec104_exception.h"
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
    const std::string SINGLE_POINT_WITH_TIMESTAMP_CONFIG_VALUE("single_time");
    const std::string MEASURED_VALUE_SHORT_WITH_TIMESTAMP_CONFIG_VALUE("short_time");
    const std::string MEASURED_VALUE_SCALED_WITH_TIMESTAMP_CONFIG_VALUE("scaled_time");

    const std::unordered_map<std::string, TIecInformationObjectType> Types = {
        {SINGLE_POINT_CONFIG_VALUE, SinglePoint},
        {MEASURED_VALUE_SHORT_CONFIG_VALUE, MeasuredValueShort},
        {MEASURED_VALUE_SCALED_CONFIG_VALUE, MeasuredValueScaled},
        {SINGLE_POINT_WITH_TIMESTAMP_CONFIG_VALUE, SinglePointWithTimestamp},
        {MEASURED_VALUE_SHORT_WITH_TIMESTAMP_CONFIG_VALUE, MeasuredValueShortWithTimestamp},
        {MEASURED_VALUE_SCALED_WITH_TIMESTAMP_CONFIG_VALUE, MeasuredValueScaledWithTimestamp}};

    TIecInformationObjectType GetIoType(const std::string& t)
    {
        auto it = Types.find(t);
        if (it == Types.end()) {
            throw std::runtime_error("Unknown information object type:" + t);
        }
        return it->second;
    }

    std::string GetDeviceName(const std::string& topic)
    {
        return StringSplit(topic, '/')[0];
    }

    std::string GetControlName(const std::string& topic)
    {
        return StringSplit(topic, '/')[1];
    }

    bool IsValidTopic(const std::string& topic)
    {
        auto l = StringSplit(topic, '/');
        return (l.size() == 2);
    }

    void LoadControls(TDeviceConfig& config, const Json::Value& controls, std::set<uint32_t>& UsedAddresses)
    {
        for (const auto& control: controls) {
            bool enabled = false;
            Get(control, "enabled", enabled);
            if (enabled) {
                auto topic = control["topic"].asString();
                if (IsValidTopic(topic)) {
                    uint32_t ioa = control["address"].asUInt();
                    if (UsedAddresses.count(ioa)) {
                        LOG(Warn) << "Control '" << topic << "' has duplicate address " << ioa;
                    } else {
                        UsedAddresses.insert(ioa);
                        config[GetDeviceName(topic)].insert(
                            {GetControlName(topic), {ioa, GetIoType(control["iec_type"].asString())}});
                    }
                } else {
                    LOG(Warn) << "Control '" << topic << "' has invalid topic name";
                }
            }
        }
    }

    TDeviceConfig LoadGroups(const Json::Value& config, std::set<uint32_t>& UsedAddresses)
    {
        TDeviceConfig res;
        bool anyEnabled = false;
        for (const auto& group: config["groups"]) {
            bool enabled = false;
            Get(group, "enabled", enabled);
            if (enabled) {
                anyEnabled = true;
                LoadControls(res, group["controls"], UsedAddresses);
            }
        }
        if (!anyEnabled) {
            throw TEmptyConfigException();
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
                for (const auto& control: device["controls"]) {
                    UsedAddresses.insert(control["address"].asUInt());
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

    Json::Value MakeControlConfig(const std::string& topic,
                                  const std::string& info,
                                  uint32_t addr,
                                  const std::string& iecType)
    {
        Json::Value cnt(Json::objectValue);
        cnt["topic"] = topic;
        cnt["info"] = info;
        cnt["enabled"] = false;
        cnt["address"] = addr;
        cnt["iec_type"] = iecType;
        return cnt;
    }

    void AppendControl(Json::Value& root, PControl c, AddressAssigner& aa)
    {
        if (!IsConvertibleControl(c)) {
            ::Warn.Log() << "'" << c->GetId() << "' of type '" << c->GetType() << "' from device '"
                         << c->GetDevice()->GetId() << "' is not convertible to IEC 608760-5-104 information object";
            return;
        }

        std::string info(c->GetType());
        info += (c->IsReadonly() ? " (read only)" : " (setup is allowed)");

        std::string controlName(c->GetDevice()->GetId() + "/" + c->GetId());

        if (c->GetType() == "switch" || c->GetType() == "pushbutton") {
            root.append(MakeControlConfig(controlName, info, aa.GetAddress(controlName), SINGLE_POINT_CONFIG_VALUE));
            return;
        }
        root.append(
            MakeControlConfig(controlName, info, aa.GetAddress(controlName), MEASURED_VALUE_SHORT_CONFIG_VALUE));
    }

    Json::Value MakeControlsConfig(std::map<std::string, PControl>& controls, AddressAssigner& addressAssigner)
    {
        Json::Value res(Json::arrayValue);
        for (auto control: controls) {
            AppendControl(res, control.second, addressAssigner);
        }
        return res;
    }

    Json::Value MakeGroupConfig(const std::string& name)
    {
        Json::Value dev(Json::objectValue);
        dev["name"] = name;
        dev["enabled"] = false;
        dev["controls"] = Json::Value(Json::arrayValue);
        return dev;
    }

    Json::Value& GetGroup(Json::Value& config, const std::string& name)
    {
        for (auto& group: config["groups"]) {
            if (group["name"].asString() == name) {
                return group;
            }
        }
        return config["groups"].append(MakeGroupConfig(name));
    }
}

TConfig LoadConfig(const std::string& configFileName, const std::string& configSchemaFileName)
{
    try {
        auto config = JSON::Parse(configFileName);
        JSON::Validate(config, JSON::Parse(configSchemaFileName));
        std::set<uint32_t> usedAddresses;

        TConfig cfg;
        cfg.Iec.BindIp = config["iec104"]["host"].asString();
        cfg.Iec.BindPort = config["iec104"]["port"].asUInt();
        cfg.Iec.CommonAddress = config["iec104"]["address"].asUInt();
        cfg.Mqtt = LoadMqttConfig(config);
        cfg.Devices = LoadGroups(config, usedAddresses);
        Get(config, "debug", cfg.Debug);
        return cfg;
    } catch (const TEmptyConfigException& e) {
        throw;
    } catch (const std::exception& e) {
        throw TConfigException(e.what());
    }
}

void UpdateConfig(const string& configFileName, const string& configSchemaFileName)
{
    const auto id = "wb-mqtt-iec104-config_generator";
    auto config = JSON::Parse(configFileName);
    JSON::Validate(config, JSON::Parse(configSchemaFileName));

    WBMQTT::TMosquittoMqttConfig mqttConfig(LoadMqttConfig(config));
    mqttConfig.Id = id;
    auto mqtt = NewMosquittoMqttClient(mqttConfig);
    auto backend = NewDriverBackend(mqtt);
    auto driver = NewDriver(TDriverArgs{}.SetId(id).SetBackend(backend));
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

    std::map<std::string, std::map<std::string, PControl>> mqttDevices;
    auto tx = driver->BeginTx();
    for (auto& device: tx->GetDevicesList()) {
        std::map<std::string, PControl> controls;
        for (auto& control: device->ControlsList()) {
            controls.insert({control->GetId(), control});
        }
        if (controls.size()) {
            mqttDevices.insert({device->GetId(), controls});
        }
    }

    for (auto& group: oldConfig["groups"]) {
        for (auto& control: group["controls"]) {
            auto topic = control["topic"].asString();
            if (IsValidTopic(topic)) {
                auto mqttDevice = mqttDevices.find(GetDeviceName(topic));
                if (mqttDevice != mqttDevices.end()) {
                    auto mqttControl = mqttDevice->second.find(GetControlName(topic));
                    if (mqttControl != mqttDevice->second.end()) {
                        mqttDevice->second.erase(mqttControl);
                    }
                }
                if (!mqttDevice->second.size()) {
                    mqttDevices.erase(mqttDevice);
                }
            }
        }
    }

    for (auto& mqttDevice: mqttDevices) {
        auto controls(MakeControlsConfig(mqttDevice.second, addressAssigner));
        if (controls.size()) {
            auto& configGroup = GetGroup(oldConfig, mqttDevice.first);
            for (auto& v: controls) {
                configGroup["controls"].append(v);
            }
        }
    }
}
