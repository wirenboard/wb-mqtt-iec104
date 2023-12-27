#include "config_parser.h"

#include <gtest/gtest.h>
#include <vector>

#include <wblib/json_utils.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

using namespace WBMQTT;

class TLoadConfigTest: public testing::Test
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
    // missing fields
    for (size_t i = 1; i <= 7; ++i) {
        ASSERT_THROW(LoadConfig(TestRootDir + "/bad/bad" + std::to_string(i) + ".conf", SchemaFile), std::runtime_error)
            << i;
    }

    // bad topic name
    auto c = LoadConfig(TestRootDir + "/bad/bad_topic_name.conf", SchemaFile);
    ASSERT_EQ(c.Devices.size(), 1);
    ASSERT_EQ(c.Devices["test"].size(), 1);
}

TEST_F(TLoadConfigTest, good)
{
    auto c = LoadConfig(TestRootDir + "/good/wb-mqtt-iec104.conf", SchemaFile);
    ASSERT_EQ(c.Devices.size(), 1);
    ASSERT_EQ(c.Devices["test"].size(), 6);
    const TIecInformationObjectType types[] = {SinglePoint,
                                               MeasuredValueShort,
                                               MeasuredValueScaled,
                                               SinglePointWithTimestamp,
                                               MeasuredValueShortWithTimestamp,
                                               MeasuredValueScaledWithTimestamp};
    size_t index = 0;
    for (const auto& control: c.Devices["test"]) {
        ASSERT_EQ(control.first, "test" + std::to_string(index + 1)) << index;
        ASSERT_EQ(control.second.Address, index + 1) << index;
        ASSERT_EQ(control.second.Type, types[index]) << index;
        ++index;
    }
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
    auto driver = NewDriver(TDriverArgs{}.SetId("test").SetBackend(backend));

    driver->StartLoop();

    auto tx = driver->BeginTx();
    auto device = tx->CreateDevice(TLocalDeviceArgs{}.SetId("test")).GetValue();

    device->CreateControl(tx, TControlArgs{}.SetId("test2").SetType("value")).GetValue();

    device->CreateControl(tx, TControlArgs{}.SetId("test4").SetType("rgb")).GetValue();

    auto device2 = tx->CreateDevice(TLocalDeviceArgs{}.SetId("test2")).GetValue();

    device2->CreateControl(tx, TControlArgs{}.SetId("test2").SetType("value").SetReadonly(true)).GetValue();

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