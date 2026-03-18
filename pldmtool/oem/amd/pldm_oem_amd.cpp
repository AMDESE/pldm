#include "pldm_oem_amd.hpp"

#include "../../pldm_cmd_helper.hpp"

namespace pldmtool
{

namespace oem_amd
{

using namespace pldmtool::helper;

namespace
{
std::vector<std::unique_ptr<CommandInterface>> commands;
}

/*
 *   • The file size and CRC32 fields are always stored in **big-endian**
 *     format regardless of host endianness.
 *
 *   • If checksum is disabled, the IC bit is cleared and the CRC32 field
 *     is omitted.
 *
 *   • rawData[] is assembled in this exact order:
 *
 *       rawData = [Header] + [File Size] + [File Data] + [Optional CRC]
 *
 *   • The offsets DEV_HDR_SIZE and FS_FIELD refer to the start of the file-size
 *     field and help determine where payload sections begin.
 *
 *   • Header include AMD IANA number in big-endian format
 *
 */

constexpr uint8_t SUBTYPE_SFS = 2;
constexpr unsigned int DEV_HDR_SIZE = 7;
constexpr unsigned int FS_FIELD = 4;
constexpr unsigned int CS_FIELD = 4;
constexpr unsigned int SUB_TYPE_BYTE = 5;
constexpr unsigned int COMP_CODE_BYTE = 6;
constexpr unsigned int MAX_FILE_SIZE = 0x4000;
constexpr unsigned int AMD_IANA_NUM = 0xe78;
constexpr unsigned int SFS_POLL_INTERVAL = 900;
constexpr uint8_t MCTP_IANA_REQUEST_BIT = 0x80;

enum class SfsCommands
{
    GETFWVER = 1,
    UPDATEFWVER
};

class AmdMctpSfsOp : public CommandInterface
{
  public:
    virtual ~AmdMctpSfsOp() = default;
    AmdMctpSfsOp() = delete;
    AmdMctpSfsOp(const AmdMctpSfsOp&) = delete;
    AmdMctpSfsOp(AmdMctpSfsOp&&) = default;
    AmdMctpSfsOp& operator=(const AmdMctpSfsOp&) = delete;
    AmdMctpSfsOp& operator=(AmdMctpSfsOp&&) = delete;

    explicit AmdMctpSfsOp(const char* type, const char* name, CLI::App* app,
                          SfsCommands cmd) : CommandInterface(type, name, app)
    {
        mctpPreAllocTag = true;
        pollInterval = SFS_POLL_INTERVAL;
        sfsCmd = cmd;
    }

    const std::string getInFileName() const
    {
        return inFileName;
    }
    const std::string getOutFileName() const
    {
        return outFileName;
    }
    std::pair<int, std::vector<uint8_t>> createRequestMsg() override;
    void parseResponseMsg(pldm_msg*, size_t) override;

  protected:
    SfsCommands sfsCmd;
    std::string outFileName{};
    std::string inFileName{};
    bool checksum{false};

  private:
    std::vector<uint8_t> rawData{0x7F, 0x00, 0x00, 0x0E, 0x78,
                                 MCTP_IANA_REQUEST_BIT | SUBTYPE_SFS};

    void appendInfile();
    void handleSFSResponse(const uint8_t*, size_t);
};

class GetFwVersion : public AmdMctpSfsOp
{
  public:
    explicit GetFwVersion(const char* type, const char* name, CLI::App* app) :
        AmdMctpSfsOp(type, name, app, SfsCommands::GETFWVER)
    {
        app->add_option("-e,--network-id", mctpNetworkId, "MCTP NetworkId")
            ->required();
        app->add_option("-o,--file-out", outFileName,
                        "Write out the received file");
        app->add_flag("-c,--checksum", checksum, "Append/Validate checksum");
        app->footer(R"(Example:
      pldmtool amdMctpSfs getFwVersion -m 21 -e 1 --file-out resp.bin
      pldmtool amdMctpSfs getFwVersion -m 21 -e 1 --file-out resp.bin --checksum)");
    }
};

class UpdateFwVersion : public AmdMctpSfsOp
{
  public:
    explicit UpdateFwVersion(const char* type, const char* name,
                             CLI::App* app) :
        AmdMctpSfsOp(type, name, app, SfsCommands::UPDATEFWVER)
    {
        app->add_option("-e,--network-id", mctpNetworkId, "MCTP NetworkId")
            ->required();
        app->add_option("-i,--file-in", inFileName,
                        "Read in the file to be sent");
        app->add_option("-o,--file-out", outFileName,
                        "Write out the received file");
        app->add_flag("-c,--checksum", checksum, "Append/Validate checksum");
        app->footer(R"(Example:
      pldmtool amdMctpSfs updateFwVersion -m 21 -e 1 --file-in req.bin
      pldmtool amdMctpSfs updateFwVersion -m 21 -e 1 --file-in req.bin --checksum
      pldmtool amdMctpSfs updateFwVersion -m 21 -e 1 --file-in req.bin --file-out /tmp/resp.bin --checksum)");
    }
};

/**
 * @brief Convert a native-endian value to big-endian.
 *
 * This function performs a byte swap on little-endian systems so the returned
 * value is always in big-endian format. On big-endian systems, the value is
 * returned unchanged.
 *
 * @tparam T  An integral type (e.g., uint16_t, uint32_t, uint64_t).
 * @param value  The value to convert.
 * @return The value represented in big-endian byte order.
 */

template <typename T>
constexpr T convToBigEndian(T value)
{
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(value);
    else
        return value;
}

/**
 * @brief Append the contents of the input file into the raw MCTP payload.
 *
 * This function performs the following steps:
 *  - Retrieves the file size using std::filesystem.
 *  - Reads the full file into memory.
 *  - Appends the 4-byte big-endian file size field to @ref rawData.
 *  - Appends the file contents to @ref rawData.
 *  - Clears or sets the IC (Integrity Check) bit in the first byte.
 *  - If checksum support is enabled, computes a CRC32 over the file data and
 *    appends it in big-endian format.
 *
 * Exceptions during file I/O (open/read) are handled via
 * std::ios_base::failure. Filesystem errors (e.g. file not found) are caught
 * via std::filesystem::filesystem_error.
 *
 * @return void
 *
 * @note Files larger than 16 MiB are not expected by design.
 * @note The function reserves enough space in @ref rawData to avoid repeated
 * reallocations.
 */

void AmdMctpSfsOp::appendInfile()
{
    std::uintmax_t fSize = 0;
    std::ifstream toDevice;
    std::vector<uint8_t> fData;

    try
    {
        fSize = std::filesystem::file_size(getInFileName());
    }
    catch (const std::filesystem::filesystem_error& fse)
    {
        std::cerr << "File size error: " << fse.what() << std::endl;
        return;
    }

    if (!fSize)
    {
        std::cerr << "Empty file: " << getInFileName() << std::endl;
        return;
    }

    try
    {
        toDevice.exceptions(std::ios::failbit | std::ios::badbit);

        toDevice.open(getInFileName(), std::ios::binary);

        fData.resize(fSize);

        toDevice.read(reinterpret_cast<char*>(fData.data()),
                      static_cast<std::streamsize>(fSize));
    }
    catch (const std::ios_base::failure& iose)
    {
        std::cerr << "In file I/O error: " << iose.what() << getInFileName()
                  << std::endl;
        return;
    }

    // Reserve expected additional (in) payload size
    std::size_t reserveSize = FS_FIELD + fSize + (checksum ? CS_FIELD : 0);
    rawData.reserve(rawData.size() + reserveSize);

    uint32_t fsField = convToBigEndian(static_cast<uint32_t>(fSize));
    auto fsValue = std::bit_cast<std::array<uint8_t, sizeof(fsField)>>(fsField);
    rawData.insert(rawData.end(), fsValue.begin(), fsValue.end());

    rawData.insert(rawData.end(), fData.begin(), fData.end());

    if (checksum)
    {
        uint32_t csField = convToBigEndian(
            static_cast<uint32_t>(pldm_edac_crc32(fData.data(), fData.size())));
        auto csValue =
            std::bit_cast<std::array<uint8_t, sizeof(csField)>>(csField);
        rawData.insert(rawData.end(), csValue.begin(), csValue.end());
    }
}

/** @brief Creates a raw MCTP request message.
 *
 *  This function processes raw data to prepare an MCTP request. It specifically
 *  handles subtype SFS messages by adjusting the Integrity Check (IC) bit
 *  based on the checksum requirement and optionally appending an input file.
 *
 *  @return A pair containing the PLDM status code and the modified raw data.
 */

std::pair<int, std::vector<uint8_t>> AmdMctpSfsOp::createRequestMsg()
{
    rawData.push_back(static_cast<uint8_t>(sfsCmd));

    if (checksum)
        rawData.at(0) |= 0x80; // set the IC bit (bit 7 [0:7]) e.g. 0x7F to 0xFF

    if (!getInFileName().empty()) // only for the -i option
        appendInfile();

    return {PLDM_SUCCESS, rawData};
}

/*
 * @brief Parse and process a SFS response message received over MCTP.
 *
 * This function validates and extracts payload information from a SFS
 * response message. The following operations are performed:
 *
 *   1. Check the PLDM completion code and abort on non-zero values.
 *   2. Extract and validate the reported firmware file size.
 *   3. If an output filename is specified, extract the firmware payload data.
 *   4. If checksum validation is enabled, compute CRC32 of the payload and
 *      validate it against the received checksum.
 *   5. Write the received firmware data to the specified output file.
 *
 * Errors and validation failures are reported to stderr and result in an
 * immediate return without modifying any output file.
 *
 * @param msg   Pointer to the SFS message buffer.
 * @param size  Total size of the SFS message in bytes.
 *
 * @note The function expects the message to contain AMD-specific vendor data
 *       with fields in big-endian format as defined by the device protocol.
 */

void AmdMctpSfsOp::handleSFSResponse(const uint8_t* msg, size_t size)
{
    // Check Completion Code (byte)
    if (msg[COMP_CODE_BYTE] != 0)
    {
        std::cerr << "Command failed with completion code: "
                  << std::showbase << std::hex
                  << static_cast<int>(msg[COMP_CODE_BYTE]) << std::dec
                  << std::endl;
        return;
    }

    if (size < (DEV_HDR_SIZE + FS_FIELD))
    {
        std::cerr << "Received response size," << size
                  << " does not include size of the file" << std::endl;
        return;
    }

    // Extract four bytes to obtain the received file size
    uint32_t fSize;

    std::memcpy(&fSize, msg + DEV_HDR_SIZE,
                sizeof(fSize)); // File size is in Big endian format as per spec
    fSize = convToBigEndian(fSize);

    if (fSize > MAX_FILE_SIZE)
    {
        std::cerr << "Error: File size " << fSize
                  << " exceeds maximum limit of " << MAX_FILE_SIZE << std::endl;
        return;
    }

    // If -o option is not specified, nothing to write to
    if (getOutFileName().empty())
        return;

    if (size < (DEV_HDR_SIZE + FS_FIELD + fSize))
    {
        std::cerr << "Received response of " << size
                  << " bytes shorter than expected size of "
                  << ((DEV_HDR_SIZE + FS_FIELD + fSize)) << std::endl;
        return;
    }

    std::vector<uint8_t> fData{msg + DEV_HDR_SIZE + FS_FIELD,
                               msg + DEV_HDR_SIZE + FS_FIELD + fSize};

    if (checksum)
    {
        if (size < (DEV_HDR_SIZE + FS_FIELD + fSize + CS_FIELD))
        {
            std::cerr << "Received response of " << size
                      << " bytes does not contain checksum" << std::endl;
            return;
        }

        // Calculate checksum
        const uint32_t crc = pldm_edac_crc32(fData.data(), fData.size());

        // Extract four bytes of checksum
        uint32_t checkSum;

        std::memcpy(
            &checkSum, msg + DEV_HDR_SIZE + FS_FIELD + fSize,
            sizeof(checkSum)); // Checksum is in Big endian format as per spec
        checkSum = convToBigEndian(checkSum);

        if (crc != checkSum)
        {
            std::cerr << std::showbase << std::hex << "CRC: " << crc
                      << " does not match the received CRC: " << checkSum
                      << std::endl;
            return;
        }
    }

    std::ofstream fromDevice;

    try
    {
        fromDevice.exceptions(std::ios::failbit | std::ios::badbit);

        fromDevice.open(getOutFileName(), std::ios::binary);

        fromDevice.write(reinterpret_cast<const char*>(fData.data()),
                         fData.size());

        std::cerr << "Created file: " << getOutFileName()
                  << " of size (in bytes): " << fData.size() << std::endl;
    }
    catch (const std::ios_base::failure& iose)
    {
        std::cerr << "Out file I/O error: " << iose.what() << getOutFileName()
                  << std::endl;
        return;
    }
}

/*
 * @brief Parse and process a PLDM response message received over MCTP.
 *
 * This function validates and extracts payload information from a raw PLDM
 * response message. The following operations are performed:
 *
 *   1. Check whether the response is for a non-SFS request.
 *   2. Validate input pointer and minimum message size.
 *   3. Verify the 4-byte IANA number (big-endian) against the expected AMD
 *      identifier.
 *   3. Check whether the response for SFS sub type SFS and if it is, call
 *      SFS response handling code.
 *
 * Errors and validation failures are reported to stderr and result in an
 * immediate return without modifying any output file.
 *
 * @param pmsg  Pointer to the raw PLDM message buffer.
 * @param size  Total size of the PLDM message in bytes.
 *
 * @note The function expects the message to contain AMD-specific vendor data
 *       with fields in big-endian format as defined by the device protocol.
 */

void AmdMctpSfsOp::parseResponseMsg(pldm_msg* pmsg, size_t size)
{
    // If this response is not for one of the file sharing protocols (e.g. SFS),
    // return
    if ((rawData.at(0) & 0x7F) != 0x7F)
        return;

    if (!pmsg)
        return;

    // Add back the size, that response receiver deducts for PLDM message header
    // does not apply in this case.
    size += sizeof(pldm_msg_hdr);

    if (size < DEV_HDR_SIZE)
    {
        std::cerr << "Invalid respose size (in bytes): " << size
                  << ", minimum expected response size: " << DEV_HDR_SIZE
                  << std::endl;
        return;
    }

    const uint8_t* msg{reinterpret_cast<uint8_t*>(pmsg)};

    // Extract four bytes of IANA number to verify
    uint32_t iana;
    memcpy(&iana, msg, sizeof(iana)); // IANA number in big endian format
    iana = convToBigEndian(iana);
    if (iana != AMD_IANA_NUM)
    {
        std::cerr << std::showbase << std::hex
                  << "Received IANA number: " << iana
                  << " does not match with: " << AMD_IANA_NUM << std::endl;
        return;
    }

    // Check SubType
    if ((rawData.at(SUB_TYPE_BYTE) & 0xF) == SUBTYPE_SFS)
        handleSFSResponse(msg, size);
}

void registerCommand(CLI::App& app)
{
    auto amdMctpSfs = app.add_subcommand("amdMctpSfs", "AMD SFS commands");
    amdMctpSfs->require_subcommand(1);

    auto getFwVersion =
        amdMctpSfs->add_subcommand("getFwVersion", "Get Firmware Version");
    commands.push_back(std::make_unique<GetFwVersion>(
        "amdMctpSfs", "GetFwVersion", getFwVersion));

    auto updateFwVersion = amdMctpSfs->add_subcommand(
        "updateFwVersion", "Update Firmware Version");
    commands.push_back(std::make_unique<UpdateFwVersion>(
        "amdMctpSfs", "UpdateFwVersion", updateFwVersion));
}

} // namespace oem_amd
} // namespace pldmtool
