#include <gtest/gtest.h>
#include <wblib/testing/testlog.h>

int main(int argc, char* argv[])
{
    WBMQTT::Testing::TLoggedFixture::SetExecutableName(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
