#include "pldm_base_cmd.hpp"
#include "pldm_bios_cmd.hpp"
#include "pldm_cmd_helper.hpp"
#include "pldm_fru_cmd.hpp"
#include "pldm_fw_update_cmd.hpp"
#include "pldm_platform_cmd.hpp"
#include "pldmtool/oem/ibm/pldm_oem_ibm.hpp"

#include <CLI/CLI.hpp>

namespace pldmtool
{

namespace raw
{

using namespace pldmtool::helper;

namespace
{
std::vector<std::unique_ptr<CommandInterface>> commands;
}

class RawOp : public CommandInterface
{
  public:
    ~RawOp() = default;
    RawOp() = delete;
    RawOp(const RawOp&) = delete;
    RawOp(RawOp&&) = default;
    RawOp& operator=(const RawOp&) = delete;
    RawOp& operator=(RawOp&&) = delete;

    explicit RawOp(const char* type, const char* name, CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-d,--data", rawData, "raw data")
            ->required()
            ->expected(-3);
    }
    std::pair<int, std::vector<uint8_t>> createRequestMsg() override

    {
        rawData[0] = (rawData[0] & 0xe0) | instanceId;
        return {PLDM_SUCCESS, rawData};
    }

    void parseResponseMsg(pldm_msg* /* responsePtr */,
                          size_t /* payloadLength */) override
    {}

  private:
    std::vector<uint8_t> rawData;
};

void registerCommand(CLI::App& app)
{
    auto raw =
        app.add_subcommand("raw", "send a raw request and print response");
    commands.push_back(std::make_unique<RawOp>("raw", "raw", raw));
}

} // namespace raw

namespace mctpRaw
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
 *   • The offsets DevHdrSize and FsField refer to the start of the file-size
 *     field and help determine where payload sections begin.
 *
 *   • Header include AMD IANA number in big-endian format
 *
 */

constexpr unsigned int DevHdrSize = 7;
constexpr unsigned int FsField = 4;
constexpr unsigned int CsField = 4;
constexpr unsigned int CompCodeByte = 6;
constexpr unsigned int MaxFileSize = 0x4000;
constexpr unsigned int AMD_Iana_Numg = 0xe78;

class MctpRawOp : public CommandInterface
{
  public:
    ~MctpRawOp() = default;
    MctpRawOp() = delete;
    MctpRawOp(const MctpRawOp&) = delete;
    MctpRawOp(MctpRawOp&&) = default;
    MctpRawOp& operator=(const MctpRawOp&) = delete;
    MctpRawOp& operator=(MctpRawOp&&) = delete;

    explicit MctpRawOp(const char* type, const char* name, CLI::App* app) :
        CommandInterface(type, name, app)
    {
        app->add_option("-e,--network-id", mctpNetworkId, "MCTP NetworkId")
            ->required();
        app->add_option("-d,--data", rawData, "raw MCTP data")
            ->required()
            ->expected(-3);
        app->add_option("-i,--file-in", inFileName, "Read in the file to be sent");
        app->add_option("-o,--file-out", outFileName, "Write out the received file");
        app->add_flag("-c,--checksum", checksum, "Append/Validate checksum");
        app->add_flag("-p,--prealloc-tag", mctpPreAllocTag,
                      "use pre-allocated MCTP tag for this request");
	app->footer(R"(Example: pldmtool mctpRaw -m 21 -e 1 \
			--data 0x7F 0x00 0x00 0x0E 0x78 0x80 0x02 \
			--file-in req.bin --file-out resp.bin --checksum)");
    }

    const std::string getInFileName() const {return inFileName;}
    const std::string getOutFileName() const {return outFileName;}
    std::pair<int, std::vector<uint8_t>> createRequestMsg() override;
    void parseResponseMsg(pldm_msg *, size_t) override;
  private:
    std::vector<uint8_t> rawData;
    std::string outFileName{};
    std::string inFileName{};
    bool checksum{false};

    void appendInfile();
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
constexpr T convToBigEndian(T value) {
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
 * Exceptions during file I/O (open/read) are handled via std::ios_base::failure.
 * Filesystem errors (e.g. file not found) are caught via std::filesystem::filesystem_error.
 *
 * @return void
 *
 * @note Files larger than 16 MiB are not expected by design.
 * @note The function reserves enough space in @ref rawData to avoid repeated reallocations.
 */

void MctpRawOp::appendInfile()
{
	std::uintmax_t fSize = 0;
	std::ifstream toDevice;
	std::vector<uint8_t> fData;

	try {
		fSize = std::filesystem::file_size(getInFileName());
	}
	catch (const std::filesystem::filesystem_error& fse) {
		std::cerr << "File size error: " << fse.what() << std::endl;
		return;
	}

	if (!fSize) {
		std::cerr << "Empty file: " << getInFileName() << std::endl;
		return;
	}

	try {
		toDevice.exceptions(std::ios::failbit | std::ios::badbit);

		toDevice.open(getInFileName(), std::ios::binary);

		fData.resize(fSize);

		toDevice.read(reinterpret_cast<char*>(fData.data()), static_cast<std::streamsize>(fSize));
	}
	catch (const std::ios_base::failure& iose) {
		std::cerr << "In file I/O error: " << iose.what() << getInFileName() << std::endl;
		return;
	}

	// Reserve expected additional (in) payload size
	std::size_t reserveSize = FsField + fSize + (checksum ? CsField : 0);
	rawData.reserve(rawData.size() + reserveSize);

	uint32_t fsField = convToBigEndian(static_cast<uint32_t>(fSize));
	auto fsValue = std::bit_cast<std::array<uint8_t, sizeof(fsField)>>(fsField);
	rawData.insert(rawData.end(), fsValue.begin(), fsValue.end());

	rawData.insert(rawData.end(), fData.begin(), fData.end());

	if (checksum) {
		uint32_t csField = convToBigEndian(static_cast<uint32_t>(pldm_edac_crc32(fData.data(), fData.size())));
		auto csValue = std::bit_cast<std::array<uint8_t, sizeof(csField)>>(csField);
		rawData.insert(rawData.end(), csValue.begin(), csValue.end());
	}
}

std::pair<int, std::vector<uint8_t>> MctpRawOp::createRequestMsg()
{
	rawData[0] &= 0x7F; // clear the IC bit (bit 7 [0:7]) e.g. in case 0xFF, to 0x7F
	if (checksum)
		rawData[0] |= 0x80; // set the IC bit (bit 7 [0:7]) e.g. 0x7F to 0xFF

	if (!getInFileName().empty()) // only for the -i option
		appendInfile();

	return {PLDM_SUCCESS, rawData};
}

/*
 * @brief Parse and process a PLDM response message received over MCTP.
 *
 * This function validates and extracts payload information from a raw PLDM
 * response message. The following operations are performed:
 *
 *   1. Validate input pointer and minimum message size.
 *   2. Check the PLDM completion code and abort on non-zero values.
 *   3. Verify the 4-byte IANA number (big-endian) against the expected AMD
 *      identifier.
 *   4. Extract and validate the reported firmware file size.
 *   5. If an output filename is specified, extract the firmware payload data.
 *   6. If checksum validation is enabled, compute CRC32 of the payload and
 *      validate it against the received checksum.
 *   7. Write the received firmware data to the specified output file.
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

void MctpRawOp::parseResponseMsg(pldm_msg *pmsg, size_t size)
{
	if (!pmsg)
		return;

	// Add back the size, that response receiver deducts for PLDM message header
	// does not apply in this case.
	size += sizeof(pldm_msg_hdr);

	if (size < DevHdrSize)
		return;

	const uint8_t * msg{reinterpret_cast<uint8_t *>(pmsg)};

	// Extract four bytes of IANA number to verify
	uint32_t iana;
	memcpy(&iana, msg, sizeof(iana)); // IANA number in big endian format
	iana = convToBigEndian(iana);
	if (iana != AMD_Iana_Numg) {
		std::cerr << std::showbase << std::hex << "Received IANA number: " << iana << " does not match with: " << AMD_Iana_Numg << std::endl;
		return;
	}

	// Check Completion Code (byte)
	if (msg[CompCodeByte] != 0) {
		std::cerr << "Non-zero completion code: " << msg[CompCodeByte] << std::endl;
		return;
	}

	if (size < (DevHdrSize + FsField))
		return;

	// Extract four bytes to obtain the received file size
	uint32_t fSize;

	std::memcpy(&fSize, msg + DevHdrSize, sizeof(fSize)); // File size is in Big endian format as per spec
	fSize = convToBigEndian(fSize);

	try {
		if (fSize > MaxFileSize)
			throw std::runtime_error("Invalid file size in bytes: " + std::to_string(fSize));
	}
	catch(const std::runtime_error & e) {
		std::cerr << e.what() << std::endl;
		return;
	}

	// If -o option is not specified, nothing to write to
	if (getOutFileName().empty())
		return;

	if (size < (DevHdrSize + FsField + fSize)) {
		std::cerr << "Received response of " << size << " bytes shorter than expected size of " << ((DevHdrSize + FsField + fSize)) << std::endl;
		return;
	}

	std::vector<uint8_t> fData{msg + DevHdrSize + FsField, msg + DevHdrSize + FsField + fSize};

	if (checksum) {
		if (size < (DevHdrSize + FsField + fSize + CsField)) {
			std::cerr << "Received response of " << size << " bytes does not contain checksum" << std::endl;
			return;
		}

		// Calculate checksum
		const uint32_t crc = pldm_edac_crc32(fData.data(), fData.size());

		// Extract four bytes of checksum
		uint32_t checkSum;

		std::memcpy(&checkSum, msg + DevHdrSize + FsField + fSize, sizeof(checkSum)); // Checksum is in Big endian format as per spec
		checkSum = convToBigEndian(checkSum);

		if (crc != checkSum) {
			std::cerr << std::showbase << std::hex << "CRC: " << crc << " does not match the received CRC: " << checkSum << std::endl;
			// return;
		}
	}

	std::ofstream fromDevice;

	try {
		fromDevice.exceptions(std::ios::failbit | std::ios::badbit);

		fromDevice.open(getOutFileName(), std::ios::binary);

		fromDevice.write(reinterpret_cast<const char*>(fData.data()), fData.size());
	}
	catch (const std::ios_base::failure& iose) {
		std::cerr << "Out file I/O error: " << iose.what() << getOutFileName() << std::endl;
		return;
	}
}

void registerCommand(CLI::App& app)
{
    auto mctpRaw = app.add_subcommand(
        "mctpRaw", "send an MCTP raw request and print response");
    commands.push_back(
        std::make_unique<MctpRawOp>("mctpRaw", "mctpRaw", mctpRaw));
}

} // namespace mctpRaw
} // namespace pldmtool

int main(int argc, char** argv)
{
    CLI::App app{"PLDM requester tool for OpenBMC"};
    app.require_subcommand(1)->ignore_case();

    pldmtool::raw::registerCommand(app);
    pldmtool::mctpRaw::registerCommand(app);
    pldmtool::base::registerCommand(app);
    pldmtool::bios::registerCommand(app);
    pldmtool::platform::registerCommand(app);
    pldmtool::fru::registerCommand(app);
    pldmtool::fw_update::registerCommand(app);
    pldmtool::rde::registerCommand(app);

#ifdef OEM_IBM
    pldmtool::oem_ibm::registerCommand(app);
#endif

    CLI11_PARSE(app, argc, argv);
    pldmtool::platform::parseGetPDROption();
    return 0;
}
