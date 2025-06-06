#include "rde/redfish_resource_manager.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <ranges>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace pldm::rde;

constexpr uint32_t kResourceID = 1001;
const std::string kPropContainerName = "Power";
const std::string kSubURI = "/redfish/v1/Chassis/1/Power";
const std::string kSchemaName = "Power.v1_0_0";
const std::string kSchemaVersion = "1.0.0.0";
const std::vector<std::string> kOEMNames = {"OEM1", "OEM2"};

// Mock subclass
class MockRedfishResourceManager : public RedfishResourceManager
{
  public:
    using RedfishResourceManager::RedfishResourceManager;

    MOCK_METHOD(std::vector<ResourceInfoView>, extractResourcePDRs, (),
                (override));
};

class RedfishResourceManagerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        mockManager = std::make_unique<MockRedfishResourceManager>(
            dummyPdrRepo, dummyDeviceID);
    }

    pldm_pdr* dummyPdrRepo = nullptr;
    uint32_t dummyDeviceID = 42;
    std::unique_ptr<MockRedfishResourceManager> mockManager;
};

TEST_F(RedfishResourceManagerTest, ExportResourceSchemaToFile)
{
    std::vector<ResourceInfoView> mockResources = {
        {kResourceID, kPropContainerName, kSubURI, kSchemaName, kSchemaVersion,
         kOEMNames}};

    EXPECT_CALL(*mockManager, extractResourcePDRs())
        .Times(1)
        .WillOnce(testing::Return(mockResources));

    std::string outputPath = "test_output_schema.json";
    mockManager->exportResourceSchemaToFile(outputPath);

    std::ifstream inFile(outputPath);
    ASSERT_TRUE(inFile.is_open());

    nlohmann::json jsonOutput;
    inFile >> jsonOutput;
    inFile.close();

    ASSERT_FALSE(jsonOutput.empty());
    ASSERT_TRUE(jsonOutput.is_object());

    ASSERT_TRUE(jsonOutput.contains("Resources"));
    const auto& resources = jsonOutput["Resources"];
    ASSERT_TRUE(resources.contains(std::to_string(kResourceID)));
    const auto& resource = resources[std::to_string(kResourceID)];

    EXPECT_EQ(resource["ProposedContainingResourceName"], kPropContainerName);
    EXPECT_EQ(resource["SubURI"], kSubURI);
    EXPECT_EQ(resource["MajorSchemaName"], kSchemaName);
    EXPECT_EQ(resource["MajorSchemaVersion"], kSchemaVersion);

    ASSERT_TRUE(resource["OEMExtensions"].is_array());
    EXPECT_EQ(resource["OEMExtensions"].size(), kOEMNames.size());

    for (auto [i, oem] : std::views::enumerate(kOEMNames))
    {
        EXPECT_EQ(resource["OEMExtensions"][i], oem);
    }

    std::remove(outputPath.c_str());
}
