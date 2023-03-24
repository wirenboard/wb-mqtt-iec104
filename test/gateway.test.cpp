#include "gateway.h"
#include "config_parser.h"

#include <gtest/gtest.h>
#include <vector>

#include <wblib/json_utils.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

using namespace WBMQTT;

namespace
{
    void Dump(Testing::TLoggedFixture& fixture, const IEC104::TInformationObjects& obj)
    {
        for (auto v: obj.SinglePoint) {
            fixture.Emit() << "SP: " << v.Address << " = " << v.Value;
        }
        for (auto v: obj.MeasuredValueShort) {
            fixture.Emit() << "MShort: " << v.Address << " = " << v.Value;
        }
        for (auto v: obj.MeasuredValueScaled) {
            fixture.Emit() << "MScaled: " << v.Address << " = " << v.Value;
        }
        for (auto v: obj.SinglePointWithTimestamp) {
            fixture.Emit() << "SP: " << v.Address << " = " << v.Value << ", with timestamp";
        }
        for (auto v: obj.MeasuredValueShortWithTimestamp) {
            fixture.Emit() << "MShort: " << v.Address << " = " << v.Value << ", with timestamp";
        }
        for (auto v: obj.MeasuredValueScaledWithTimestamp) {
            fixture.Emit() << "MScaled: " << v.Address << " = " << v.Value << ", with timestamp";
        }
    }
}

class TGatewayTest: public Testing::TLoggedFixture
{
protected:
    std::string TestRootDir;
    std::string SchemaFile;
    PDeviceDriver Driver;
    TDeviceConfig Config;
    PControl Control1;
    PControl Control2;
    PControl Control3;
    PControl Control4;
    PControl Control5;
    PControl Control6;
    PControl ControlNotInConfig;

    void SetUp()
    {
        TestRootDir = GetDataFilePath("gateway_test_data");
        SchemaFile = TestRootDir + "/../../wb-mqtt-iec104.schema.json";

        auto mqttBroker = Testing::NewFakeMqttBroker(*this);
        auto mqttClient = mqttBroker->MakeClient("test");
        auto backend = NewDriverBackend(mqttClient);
        Driver = NewDriver(TDriverArgs{}.SetId("test").SetBackend(backend));

        Driver->StartLoop();
        Driver->WaitForReady();

        Config = LoadConfig(TestRootDir + "/config.conf", SchemaFile).Devices;

        auto tx = Driver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}.SetId("test")).GetValue();

        Control1 = device->CreateControl(tx, TControlArgs{}.SetId("test1").SetType("value").SetValue(1.23)).GetValue();

        Control2 =
            device->CreateControl(tx, TControlArgs{}.SetId("test2").SetType("switch").SetValue(false)).GetValue();

        Control3 = device->CreateControl(tx, TControlArgs{}.SetId("test3").SetType("value").SetValue(123)).GetValue();

        Control4 = device->CreateControl(tx, TControlArgs{}.SetId("test4").SetType("value").SetValue(3.21)).GetValue();

        Control5 = device->CreateControl(tx, TControlArgs{}.SetId("test5").SetType("switch").SetValue(true)).GetValue();

        Control6 = device->CreateControl(tx, TControlArgs{}.SetId("test6").SetType("value").SetValue(321)).GetValue();

        ControlNotInConfig =
            device->CreateControl(tx, TControlArgs{}.SetId("ControlNotInConfig").SetType("value").SetValue(1.23))
                .GetValue();
        tx->End();
    }
};

class TFakeIecServer: public IEC104::IServer
{
    Testing::TLoggedFixture& Fixture;

public:
    TFakeIecServer(Testing::TLoggedFixture& fixture): Fixture(fixture)
    {}

    void Stop()
    {}

    void SendSpontaneous(const IEC104::TInformationObjects& obj)
    {
        Fixture.Emit() << "IEC104::IServer::SendSpontaneous";
        Dump(Fixture, obj);
    }

    void SetHandler(IEC104::IHandler* handler)
    {}
};

TEST_F(TGatewayTest, SetParameter)
{
    TFakeIecServer iecServer(*this);
    TGateway gw(Driver, &iecServer, Config);

    // Valid params
    ASSERT_TRUE(gw.SetParameter(1, "10.21"));
    ASSERT_TRUE(gw.SetParameter(2, "1"));
    ASSERT_TRUE(gw.SetParameter(3, "-1"));
    ASSERT_TRUE(gw.SetParameter(4, "9.87"));
    ASSERT_TRUE(gw.SetParameter(5, "0"));
    ASSERT_TRUE(gw.SetParameter(6, "-15"));

    // Unknown ioa
    ASSERT_FALSE(gw.SetParameter(7, "7"));
}

TEST_F(TGatewayTest, GetInformationObjectsValues)
{
    TFakeIecServer iecServer(*this);
    TGateway gw(Driver, &iecServer, Config);
    Dump(*this, gw.GetInformationObjectsValues());
}

TEST_F(TGatewayTest, OnValueChanged)
{
    TFakeIecServer iecServer(*this);
    TGateway gw(Driver, &iecServer, Config);
    auto tx = Driver->BeginTx();
    ControlNotInConfig->SetValue(tx, 123.123).Sync();
    Control1->SetValue(tx, 2.34).Sync();
    Control2->SetValue(tx, true).Sync();
    Control3->SetRawValue(tx, "200").Sync();
    Control4->SetValue(tx, 5.67).Sync();
    Control5->SetValue(tx, false).Sync();
    Control6->SetRawValue(tx, "768").Sync();
    tx->End();
}
