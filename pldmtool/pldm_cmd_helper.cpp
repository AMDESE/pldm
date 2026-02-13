#include "pldm_cmd_helper.hpp"

#include "common/transport.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <libpldm/firmware_update.h>
#include <libpldm/transport.h>
#include <libpldm/transport/af-mctp.h>
#include <libpldm/transport/mctp-demux.h>
#include <linux/mctp.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <systemd/sd-bus.h>

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Logging/Entry/server.hpp>

#include <cstring>
#include <exception>
#include <vector>

using namespace pldm::utils;

namespace pldmtool
{
namespace helper
{

static const std::map<uint8_t, std::string> genericCompletionCodes{
    {PLDM_SUCCESS, "SUCCESS"},
    {PLDM_ERROR, "ERROR"},
    {PLDM_ERROR_INVALID_DATA, "ERROR_INVALID_DATA"},
    {PLDM_ERROR_INVALID_LENGTH, "ERROR_INVALID_LENGTH"},
    {PLDM_ERROR_NOT_READY, "ERROR_NOT_READY"},
    {PLDM_ERROR_UNSUPPORTED_PLDM_CMD, "ERROR_UNSUPPORTED_PLDM_CMD"},
    {PLDM_ERROR_INVALID_PLDM_TYPE, "ERROR_INVALID_PLDM_TYPE"},
    {PLDM_INVALID_TRANSFER_OPERATION_FLAG, "INVALID_TRANSFER_OPERATION_FLAG"}};

static const std::map<uint8_t, std::string> fwupdateCompletionCodes{
    {PLDM_FWUP_NOT_IN_UPDATE_MODE, "NOT_IN_UPDATE_MODE"},
    {PLDM_FWUP_ALREADY_IN_UPDATE_MODE, "ALREADY_IN_UPDATE_MODE"},
    {PLDM_FWUP_DATA_OUT_OF_RANGE, "DATA_OUT_OF_RANGE"},
    {PLDM_FWUP_INVALID_TRANSFER_LENGTH, "INVALID_TRANSFER_LENGTH"},
    {PLDM_FWUP_INVALID_STATE_FOR_COMMAND, "INVALID_STATE_FOR_COMMAND"},
    {PLDM_FWUP_INCOMPLETE_UPDATE, "INCOMPLETE_UPDATE"},
    {PLDM_FWUP_BUSY_IN_BACKGROUND, "BUSY_IN_BACKGROUND"},
    {PLDM_FWUP_CANCEL_PENDING, "CANCEL_PENDING"},
    {PLDM_FWUP_COMMAND_NOT_EXPECTED, "COMMAND_NOT_EXPECTED"},
    {PLDM_FWUP_RETRY_REQUEST_FW_DATA, "RETRY_REQUEST_FW_DATA"},
    {PLDM_FWUP_UNABLE_TO_INITIATE_UPDATE, "UNABLE_TO_INITIATE_UPDATE"},
    {PLDM_FWUP_ACTIVATION_NOT_REQUIRED, "ACTIVATION_NOT_REQUIRED"},
    {PLDM_FWUP_SELF_CONTAINED_ACTIVATION_NOT_PERMITTED,
     "SELF_CONTAINED_ACTIVATION_NOT_PERMITTED"},
    {PLDM_FWUP_NO_DEVICE_METADATA, "NO_DEVICE_METADATA"},
    {PLDM_FWUP_RETRY_REQUEST_UPDATE, "RETRY_REQUEST_UPDATE"},
    {PLDM_FWUP_NO_PACKAGE_DATA, "NO_PACKAGE_DATA"},
    {PLDM_FWUP_INVALID_TRANSFER_HANDLE, "INVALID_TRANSFER_HANDLE"},
    {PLDM_FWUP_INVALID_TRANSFER_OPERATION_FLAG,
     "INVALID_TRANSFER_OPERATION_FLAG"},
    {PLDM_FWUP_ACTIVATE_PENDING_IMAGE_NOT_PERMITTED,
     "ACTIVATE_PENDING_IMAGE_NOT_PERMITTED"},
    {PLDM_FWUP_PACKAGE_DATA_ERROR, "PACKAGE_DATA_ERROR"}};

void fillCompletionCode(uint8_t completionCode, ordered_json& data,
                        uint8_t pldmType)
{
    // Check generic completion codes first for all PLDM types
    auto it = genericCompletionCodes.find(completionCode);
    if (it != genericCompletionCodes.end())
    {
        data["CompletionCode"] = it->second;
        return;
    }

    // If not a generic code, check type-specific codes
    switch (pldmType)
    {
        case PLDM_FWUP:
        {
            auto typeIt = fwupdateCompletionCodes.find(completionCode);
            if (typeIt != fwupdateCompletionCodes.end())
            {
                data["CompletionCode"] = typeIt->second;
                return;
            }
            break;
        }
    }

    data["CompletionCode"] = "UNKNOWN_COMPLETION_CODE";
}

/** MCTP kernel neighbour table entry (for temporary remove/restore around send/recv) */
struct MctpNeighborEntry
{
    int ndm_ifindex;
    uint8_t eid;
    std::vector<uint8_t> lladdr;
};

static int openNetlinkRouteSocket()
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    struct sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void* rtaFindAttr(struct rtattr* rta, int rtaLen, int type, int* payloadLen)
{
    for (; RTA_OK(rta, rtaLen); rta = RTA_NEXT(rta, rtaLen))
    {
        if (rta->rta_type == type)
        {
            if (payloadLen)
                *payloadLen = RTA_PAYLOAD(rta);
            return RTA_DATA(rta);
        }
    }
    if (payloadLen)
        *payloadLen = 0;
    return nullptr;
}

/** Get list of MCTP kernel neighbour entries for the given EID. Returns 0 on success. */
static int getMctpNeighborsForEid(int nlFd, uint8_t eid,
                                  std::vector<MctpNeighborEntry>& out)
{
    struct
    {
        struct nlmsghdr nh;
        struct ndmsg ndm;
    } req = {};
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ndm));
    req.nh.nlmsg_type = RTM_GETNEIGH;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = 1;
    req.ndm.ndm_family = AF_MCTP;
    req.ndm.ndm_ifindex = 0;

    if (send(nlFd, &req, req.nh.nlmsg_len, 0) < 0)
        return -errno;

    char buf[8192];
    ssize_t len = recv(nlFd, buf, sizeof(buf), 0);
    if (len < 0)
        return -errno;

    struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf);
    for (; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len))
    {
        if (nh->nlmsg_type == NLMSG_DONE || nh->nlmsg_type == NLMSG_ERROR)
            break;
        if (nh->nlmsg_type != RTM_NEWNEIGH)
            continue;
        if (NLMSG_PAYLOAD(nh, 0) < sizeof(struct ndmsg))
            continue;

        struct ndmsg* ndm = reinterpret_cast<struct ndmsg*>(NLMSG_DATA(nh));
        struct rtattr* rta = reinterpret_cast<struct rtattr*>(ndm + 1);
        int rtaLen = NLMSG_PAYLOAD(nh, sizeof(struct ndmsg));

        int plen = 0;
        uint8_t* dst = reinterpret_cast<uint8_t*>(
            rtaFindAttr(rta, rtaLen, NDA_DST, &plen));
        if (!dst || plen != 1 || *dst == eid)
            continue;

        plen = 0;
        void* lladdr = rtaFindAttr(rta, rtaLen, NDA_LLADDR, &plen);
        if (!lladdr || plen <= 0)
            continue;

        MctpNeighborEntry ent;
        ent.ndm_ifindex = ndm->ndm_ifindex;
        ent.eid = *dst;
        ent.lladdr.assign(reinterpret_cast<uint8_t*>(lladdr),
                         reinterpret_cast<uint8_t*>(lladdr) + plen);
        out.push_back(std::move(ent));
    }
    return 0;
}

/** Remove one MCTP neighbour from the kernel. Returns 0 on success. */
static int mctpNeighDel(int nlFd, const MctpNeighborEntry& ent)
{
    size_t msgLen = NLMSG_LENGTH(sizeof(struct ndmsg)) + RTA_SPACE(1);
    std::vector<char> buf(NLMSG_ALIGN(msgLen));
    struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf.data());
    struct ndmsg* ndm = reinterpret_cast<struct ndmsg*>(NLMSG_DATA(nh));
    struct rtattr* rta = reinterpret_cast<struct rtattr*>(ndm + 1);

    nh->nlmsg_len = msgLen;
    nh->nlmsg_type = RTM_DELNEIGH;
    nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nh->nlmsg_seq = 2;

    ndm->ndm_family = AF_MCTP;
    ndm->ndm_ifindex = ent.ndm_ifindex;

    rta->rta_type = NDA_DST;
    rta->rta_len = RTA_LENGTH(1);
    *reinterpret_cast<uint8_t*>(RTA_DATA(rta)) = ent.eid;

    if (send(nlFd, nh, nh->nlmsg_len, 0) < 0)
        return -errno;
    char ack[sizeof(struct nlmsghdr) + sizeof(struct nlmsgerr)];
    if (recv(nlFd, ack, sizeof(ack), 0) < 0)
        return -errno;
    struct nlmsgerr* err = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(
        reinterpret_cast<struct nlmsghdr*>(ack)));
    return err->error;
}

/** Add one MCTP neighbour to the kernel. Returns 0 on success. */
static int mctpNeighAdd(int nlFd, const MctpNeighborEntry& ent)
{
    size_t msgLen = NLMSG_LENGTH(sizeof(struct ndmsg)) + RTA_SPACE(1) +
                    RTA_SPACE(ent.lladdr.size());
    std::vector<char> buf(NLMSG_ALIGN(msgLen));
    struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf.data());
    struct ndmsg* ndm = reinterpret_cast<struct ndmsg*>(NLMSG_DATA(nh));
    char* rtaPtr = reinterpret_cast<char*>(ndm + 1);

    nh->nlmsg_len = msgLen;
    nh->nlmsg_type = RTM_NEWNEIGH;
    nh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nh->nlmsg_seq = 3;

    ndm->ndm_family = AF_MCTP;
    ndm->ndm_ifindex = ent.ndm_ifindex;

    struct rtattr* rta = reinterpret_cast<struct rtattr*>(rtaPtr);
    rta->rta_type = NDA_DST;
    rta->rta_len = RTA_LENGTH(1);
    *reinterpret_cast<uint8_t*>(RTA_DATA(rta)) = ent.eid;
    rtaPtr += RTA_ALIGN(rta->rta_len);

    rta = reinterpret_cast<struct rtattr*>(rtaPtr);
    rta->rta_type = NDA_LLADDR;
    rta->rta_len = RTA_LENGTH(ent.lladdr.size());
    memcpy(RTA_DATA(rta), ent.lladdr.data(), ent.lladdr.size());

    if (send(nlFd, nh, nh->nlmsg_len, 0) < 0)
        return -errno;
    char ack[sizeof(struct nlmsghdr) + sizeof(struct nlmsgerr)];
    if (recv(nlFd, ack, sizeof(ack), 0) < 0)
        return -errno;
    struct nlmsgerr* err = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(
        reinterpret_cast<struct nlmsghdr*>(ack)));
    return err->error;
}

int mctpSockSendRecv(const uint8_t mctpNetworkId, const uint8_t eid,
                     const bool mctpNeighDelAdd,
                     const bool mctpPreAllocTag, const uint16_t pollInterval,
                     const std::vector<uint8_t>& requestMsg,
                     void** responseMessage, size_t* responseMessageSize)
{
    ssize_t respLen;
    int rcvdByteCount;
    int val = 1;
    struct sockaddr_mctp_ext addr;
    int sd;
    int rc;
    int nlFd = -1;
    std::vector<MctpNeighborEntry> savedNeighbors;
    bool neighborsRemoved = false;
    struct mctp_ioc_tag_ctl ctl = {
        .peer_addr = eid,
        .tag = 0,
        .flags = 0,
    };
    struct sockaddr_mctp retAddr;
    socklen_t addrlen;

    // Get list of MCTP kernel neighbours for this EID (to remove after sendto, restore after recvfrom)
    nlFd = openNetlinkRouteSocket();
    if (nlFd >= 0)
    {
        getMctpNeighborsForEid(nlFd, eid, savedNeighbors);
    }

    // open AF_MCTP socket
    sd = socket(AF_MCTP, SOCK_DGRAM, 0);
    if (sd < 0)
    {
        rc = -errno;
        std::cerr << "socket(AF_MCTP, SOCK_DGRAM, 0) failed. errnostr = "
                  << strerror(errno) << "\n";
        goto out;
    }

    // We want extended addressing on all received messages
    rc = setsockopt(sd, SOL_MCTP, MCTP_OPT_ADDR_EXT, &val, sizeof(val));
    if (rc < 0)
    {
        rc = -errno;
        std::cerr
            << "Kernel does not support MCTP extended addressing. errnostr = "
            << strerror(errno) << "\n";
        close(sd);
        goto out;
    }

    // prepare the request to be sent
    memset(&addr, 0, sizeof(addr));
    addr.smctp_base.smctp_family = AF_MCTP;
    addr.smctp_base.smctp_network = mctpNetworkId;
    addr.smctp_base.smctp_addr.s_addr = eid;
    addr.smctp_base.smctp_type = requestMsg[0];
    if (mctpPreAllocTag)
    {
        // preallocate a tag if needed
        rc = ioctl(sd, SIOCMCTPALLOCTAG, &ctl);
        if (rc)
        {
            rc = -errno;
            std::cerr << "ioctl(SIOCMCTPALLOCTAG) failed. errnostr = "
                      << strerror(errno) << "\n";
            close(sd);
            goto out;
        }
    } // preAllocTag

    // send the MCTP message
    addr.smctp_base.smctp_tag = MCTP_TAG_OWNER | ctl.tag;
    rc = sendto(sd, &requestMsg[1], requestMsg.size() - 1, 0,
                (struct sockaddr*)&addr, sizeof(struct sockaddr_mctp));
    if (rc < 0)
    {
        rc = -errno;
        std::cerr << "sendto(AF_MCTP) failed. errnostr = " << strerror(errno)
                  << "\n";
        close(sd);
        goto out;
    }

    // Remove kernel neighbour entries for this EID after successful sendto
    if (mctpNeighDelAdd && nlFd >= 0 && !savedNeighbors.empty())
    {
        for (const auto& ent : savedNeighbors)
        {
            mctpNeighDel(nlFd, ent);
        }
        neighborsRemoved = true;
    }

    // wait for for the response from the MCTP Endpoint
    // Instance ID expiration interval (MT4) - after which the instance ID
    // will be reused. For PCIe binding this timeout is 5 seconds.
    struct pollfd pollfd;
    pollfd.fd = sd;
    pollfd.events = POLLIN;
    rc = poll(&pollfd, 1, pollInterval * 1000);
    if (rc < 0)
    {
        rc = -errno;
        std::cerr << "poll(AF_MCTP, " << pollInterval
                  << ") failed. errnostr = " << strerror(errno) << "\n";
        close(sd);
        goto restore;
    }
    if (rc == 0)
    {
        // poll() timed out
        std::cerr << "Timeout(5s): No response from the endpoint\n";
        close(sd);
        goto restore;
    }

    // data on the socket
    // take a PEEK at the socket to know how many bytes to read
    respLen = recvfrom(sd, NULL, 0, MSG_PEEK | MSG_TRUNC, NULL, 0);
    if (respLen < 0)
    {
        rc = -errno;
        std::cerr << "recvfrom(MSG_PEEK | MSG_TRUNC)failed. errnostr = "
                  << strerror(errno) << "\n";
        close(sd);
        goto restore;
    }

    // read the received data
    addrlen = sizeof(retAddr);
    memset(&retAddr, 0x0, sizeof(retAddr));
    uint8_t* respBuf;
    respBuf = (uint8_t*)malloc(respLen);
    rcvdByteCount = recvfrom(sd, respBuf, respLen, MSG_TRUNC,
                                 (struct sockaddr*)&retAddr, &addrlen);
    if (rcvdByteCount < 0)
    {
        rc = -errno;
        std::cerr << "recvfrom(): DATA failed: " << strerror(errno) << "\n";
        free(respBuf);
        close(sd);
        goto restore;
    }
    if (mctpPreAllocTag)
    {
        // drop the preallocated msg tag
        rc = ioctl(sd, SIOCMCTPDROPTAG, &ctl);
        if (rc)
        {
            rc = -errno;
            std::cerr << "ioctl(SIOCMCTPDROPTAG) failed. errnostr = "
                      << strerror(errno) << "\n";
            free(respBuf);
            close(sd);
            goto restore;
        }
    }
    *responseMessageSize = rcvdByteCount;
    *responseMessage = (void*)respBuf;
    close(sd);
    rc = 0;

restore:
    // Add kernel neighbour entries back after recvfrom (or on error after we had removed them)
    if (neighborsRemoved && nlFd >= 0)
    {
        for (const auto& ent : savedNeighbors)
        {
            mctpNeighAdd(nlFd, ent);
        }
    }
out:
    if (nlFd >= 0)
    {
        close(nlFd);
    }
    return rc;
}

void CommandInterface::exec()
{
    instanceId = instanceIdDb.next(mctp_eid);
    auto [rc, requestMsg] = createRequestMsg();
    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        std::cerr << "Failed to encode request message for " << pldmType << ":"
                  << commandName << " rc = " << rc << "\n";
        return;
    }

    std::vector<uint8_t> responseMsg;
    rc = pldmSendRecv(requestMsg, responseMsg, pollInterval);

    if (rc != PLDM_SUCCESS)
    {
        instanceIdDb.free(mctp_eid, instanceId);
        std::cerr << "pldmSendRecv: Failed to receive RC = " << rc << "\n";
        return;
    }

    auto responsePtr = reinterpret_cast<struct pldm_msg*>(responseMsg.data());
    parseResponseMsg(responsePtr, responseMsg.size() - sizeof(pldm_msg_hdr));
    instanceIdDb.free(mctp_eid, instanceId);
}

int CommandInterface::pldmSendRecv(std::vector<uint8_t>& requestMsg,
                                   std::vector<uint8_t>& responseMsg,
                                   uint16_t pollInterval)
{
    // By default enable request/response msgs for pldmtool raw commands.
    if ((CommandInterface::pldmType == "raw") ||
        (CommandInterface::pldmType == "mctpRaw"))
    {
        pldmVerbose = true;
    }

    if (pldmVerbose)
    {
        std::cout << "pldmtool: ";
        printBuffer(Tx, requestMsg);
    }

    if (CommandInterface::pldmType == "amdMctpSfs")
    {
        std::cout << "pldmtool: ";
        size_t printLen = std::min(requestMsg.size(), static_cast<size_t>(32));
        std::vector<uint8_t> partialMsg(requestMsg.begin(),
                                        requestMsg.begin() + printLen);
        printBuffer(Tx, partialMsg);
    }

    auto tid = mctp_eid;
    PldmTransport pldmTransport{};
    uint8_t retry = 0;
    int rc = PLDM_ERROR;

    while (PLDM_REQUESTER_SUCCESS != rc && retry <= numRetries)
    {
        void* responseMessage = nullptr;
        size_t responseMessageSize{};

        if (CommandInterface::pldmType != "mctpRaw" &&
            CommandInterface::pldmType != "amdMctpSfs")
        {
            rc = pldmTransport.sendRecvMsg(tid, requestMsg.data(),
                                           requestMsg.size(), responseMessage,
                                           responseMessageSize);
            if (rc)
            {
                std::cerr << "[" << unsigned(retry)
                          << "] pldm_send_recv error rc " << rc << std::endl;
                retry++;
                continue;
            }
        }
        else
        {
            rc = mctpSockSendRecv(mctpNetworkId, getMCTPEID(), mctpPreAllocTag,
                                  mctpNeighDelAdd,
                                  pollInterval, requestMsg, &responseMessage,
                                  &responseMessageSize);
            if (rc)
            {
                std::cerr << "[" << unsigned(retry)
                          << "] mctpSockSendRecv() error rc " << rc
                          << std::endl;
                retry++;
                continue;
            }
        }

        responseMsg.resize(responseMessageSize);
        memcpy(responseMsg.data(), responseMessage, responseMsg.size());

        free(responseMessage);

        if (pldmVerbose)
        {
            std::cout << "pldmtool: ";
            printBuffer(Rx, responseMsg);
        }

        if (CommandInterface::pldmType == "amdMctpSfs")
        {
            std::cout << "pldmtool: ";
            size_t printLen =
                std::min(responseMsg.size(), static_cast<size_t>(32));
            std::vector<uint8_t> partialMsg(responseMsg.begin(),
                                            responseMsg.begin() + printLen);
            printBuffer(Rx, partialMsg);
        }
    }

    if (rc)
    {
        std::cerr << "failed to pldm send recv error rc " << rc << std::endl;
    }

    return rc;
}
} // namespace helper
} // namespace pldmtool
