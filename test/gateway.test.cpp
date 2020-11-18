#include "gateway.h"
#include "config_parser.h"

#include <gtest/gtest.h>
#include <vector>

#include <wblib/testing/testlog.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/json_utils.h>

using namespace WBMQTT;

namespace
{
    void Dump(Testing::TLoggedFixture& fixture, const IEC104::TInformationObjects& obj)
    {
        for (auto v: obj.SinglePoint) {
            fixture.Emit() << "SP: " << v.first << " = " << v.second;
        }
        for (auto v: obj.MeasuredValueShort) {
            fixture.Emit() << "MShort: " << v.first << " = " << v.second;
        }
        for (auto v: obj.MeasuredValueScaled) {
            fixture.Emit() << "MScaled: " << v.first << " = " << v.second;
        }
    }
}

class TGatewayTest: public Testing::TLoggedFixture
{
protected:
    std::string   TestRootDir;
    std::string   SchemaFile;
    PDeviceDriver Driver;
    TDeviceConfig Config;
    PControl      Control1;
    PControl      Control2;
    PControl      Control3;
    PControl      Control4;
    PControl      ControlNotInConfig;

    void SetUp()
    {
        TestRootDir = GetDataFilePath("gateway_test_data");
        SchemaFile = TestRootDir + "/../../wb-mqtt-iec104.schema.json";

        auto mqttBroker = Testing::NewFakeMqttBroker(*this);
        auto mqttClient = mqttBroker->MakeClient("test");
        auto backend = NewDriverBackend(mqttClient);
        Driver = NewDriver(TDriverArgs{}
            .SetId("test")
            .SetBackend(backend)
        );

        Driver->StartLoop();
        Driver->WaitForReady();

        Config = LoadConfig(TestRootDir + "/config.conf", SchemaFile).Devices;

        auto tx = Driver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId("test")
        ).GetValue();

        Control1 = device->CreateControl(tx, TControlArgs{}
            .SetId("test1")
            .SetType("value")
            .SetValue(1.23)
        ).GetValue();

        Control2 = device->CreateControl(tx, TControlArgs{}
            .SetId("test2")
            .SetType("switch")
            .SetValue(false)
        ).GetValue();

        Control3 = device->CreateControl(tx, TControlArgs{}
            .SetId("test3")
            .SetType("value")
            .SetValue(123)
        ).GetValue();

        Control4 = device->CreateControl(tx, TControlArgs{}
            .SetId("test4")
            .SetType("rgb")
            .SetValue("10;20;30")
        ).GetValue();

        ControlNotInConfig = device->CreateControl(tx, TControlArgs{}
            .SetId("ControlNotInConfig")
            .SetType("value")
            .SetValue(1.23)
        ).GetValue();
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
    ASSERT_TRUE(gw.SetParameter(4, "5"));
    ASSERT_TRUE(gw.SetParameter(5, "6"));
    ASSERT_TRUE(gw.SetParameter(6, "7"));

    // Unknown ioa
    ASSERT_FALSE(gw.SetParameter(7, "7"));

    // New rgb value
    Control4->SetValue(Driver->BeginTx(), "").Sync();
    ASSERT_TRUE(gw.SetParameter(5, "6"));
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
    Control4->SetValue(tx, "1,2,3").Sync();
    Control4->SetValue(tx, "100;200;300").Sync();
    tx->End();
}
