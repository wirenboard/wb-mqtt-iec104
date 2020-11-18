#include "config_parser.h"

#include <gtest/gtest.h>
#include <vector>

#include <wblib/testing/testlog.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/json_utils.h>

using namespace WBMQTT;

class TLoadConfigTest : public testing::Test
{
protected:
    std::string TestRootDir;
    std::string SchemaFile;

    void SetUp()
    {
        TestRootDir = Testing::TLoggedFixture::GetDataFilePath("config_test_data");
        SchemaFile = TestRootDir + "/../../wb-mqtt-iec104.schema.json";
    }
};

TEST_F(TLoadConfigTest, no_file)
{
    ASSERT_THROW(LoadConfig("", SchemaFile), std::runtime_error);
    ASSERT_THROW(LoadConfig(TestRootDir + "/bad/wb-mqtt-iec104.conf", ""), std::runtime_error);
}

TEST_F(TLoadConfigTest, bad_config)
{
    for (size_t i = 1; i <= 7; ++i) {
        ASSERT_THROW(LoadConfig(TestRootDir + "/bad/bad" + std::to_string(i) + ".conf", SchemaFile), std::runtime_error);
    }
}

TEST_F(TLoadConfigTest, good)
{
    auto c = LoadConfig(TestRootDir + "/bad/non_unique_address.conf", SchemaFile);
    ASSERT_EQ(c.Devices.size(), 1);
    ASSERT_EQ(c.Devices["test"].size(), 1);
    ASSERT_STREQ(c.Devices["test"].begin()->first.c_str(), "test2");
    ASSERT_EQ(c.Devices["test"].begin()->second.Address, 2);
    ASSERT_EQ(c.Devices["test"].begin()->second.Type, MeasuredValueShort);
}

class TUpdateConfigTest: public Testing::TLoggedFixture
{
protected:
    std::string testRootDir;
    std::string schemaFile;

    void SetUp()
    {
        testRootDir = GetDataFilePath("config_test_data");
        schemaFile = testRootDir + "/../../wb-mqtt-iec104.schema.json";
    }
};

TEST_F(TUpdateConfigTest, update)
{
    auto mqttBroker = Testing::NewFakeMqttBroker(*this);
    auto mqttClient = mqttBroker->MakeClient("test");
    auto backend = NewDriverBackend(mqttClient);
    auto driver = NewDriver(TDriverArgs{}
        .SetId("test")
        .SetBackend(backend)
    );

    driver->StartLoop();

    auto tx = driver->BeginTx();
    auto device = tx->CreateDevice(TLocalDeviceArgs{}
        .SetId("test")
    ).GetValue();

    device->CreateControl(tx, TControlArgs{}
        .SetId("test2")
        .SetType("value")
    ).GetValue();

    device->CreateControl(tx, TControlArgs{}
        .SetId("test4")
        .SetType("rgb")
    ).GetValue();

    auto device2 = tx->CreateDevice(TLocalDeviceArgs{}
        .SetId("test2")
    ).GetValue();

    device2->CreateControl(tx, TControlArgs{}
        .SetId("test2")
        .SetType("value")
        .SetReadonly(true)
    ).GetValue();

    tx->End();

    auto c = JSON::Parse(testRootDir + "/bad/non_unique_address.conf");
    JSON::Validate(c, JSON::Parse(schemaFile));

    UpdateConfig(driver, c);

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::stringstream ss;
    writer->write(c, &ss);
    Emit() << ss.str();
}