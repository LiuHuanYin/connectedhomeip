/*
 *
 *    Copyright (c) 2021-2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "ConnectivityUtils.h"

#include <app-common/zap-generated/enums.h>
#include <platform/Tizen/ConnectivityUtils.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/wireless.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include <lib/support/CHIPMemString.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace ::chip::app::Clusters::GeneralDiagnostics;
using namespace ::chip::app::Clusters::EthernetNetworkDiagnostics;

namespace chip {
namespace DeviceLayer {
namespace Internal {

uint16_t ConnectivityUtils::MapChannelToFrequency(const uint16_t inBand, const uint8_t inChannel)
{
    uint16_t frequency = 0;

    if (inBand == kWiFi_BAND_2_4_GHZ)
    {
        frequency = Map2400MHz(inChannel);
    }
    else if (inBand == kWiFi_BAND_5_0_GHZ)
    {
        frequency = Map5000MHz(inChannel);
    }

    return frequency;
}

uint8_t ConnectivityUtils::MapFrequencyToChannel(const uint16_t frequency)
{
    if (frequency < 2412)
        return 0;

    if (frequency < 2484)
        return static_cast<uint8_t>((frequency - 2407) / 5);

    if (frequency == 2484)
        return 14;

    return static_cast<uint8_t>(frequency / 5 - 1000);
}

InterfaceTypeEnum ConnectivityUtils::GetInterfaceConnectionType(const char * ifname)
{
    InterfaceTypeEnum ret = InterfaceTypeEnum::kUnspecified;
    int sock              = -1;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        ChipLogError(DeviceLayer, "Failed to open socket");
        return ret;
    }

    // Test wireless extensions for CONNECTION_WIFI
    struct iwreq pwrq = {};
    Platform::CopyString(pwrq.ifr_name, ifname);

    if (ioctl(sock, SIOCGIWNAME, &pwrq) != -1)
    {
        ret = InterfaceTypeEnum::kWiFi;
    }
    else if ((strncmp(ifname, "en", 2) == 0) || (strncmp(ifname, "eth", 3) == 0))
    {
        struct ethtool_cmd ecmd = {};
        ecmd.cmd                = ETHTOOL_GSET;
        struct ifreq ifr        = {};
        ifr.ifr_data            = reinterpret_cast<char *>(&ecmd);
        Platform::CopyString(ifr.ifr_name, ifname);

        if (ioctl(sock, SIOCETHTOOL, &ifr) != -1)
            ret = InterfaceTypeEnum::kEthernet;
    }

    close(sock);

    return ret;
}

CHIP_ERROR ConnectivityUtils::GetInterfaceHardwareAddrs(const char * ifname, uint8_t * buf, size_t bufSize)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;
    int skfd;

    if (ifname[0] == '\0')
    {
        ChipLogError(DeviceLayer, "Invalid argument for interface name");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    struct ifreq req;
    Platform::CopyString(req.ifr_name, ifname);
    if (ioctl(skfd, SIOCGIFHWADDR, &req) != -1)
    {
        // Copy 48-bit IEEE MAC Address
        VerifyOrReturnError(bufSize >= 6, CHIP_ERROR_BUFFER_TOO_SMALL);

        memset(buf, 0, bufSize);
        memcpy(buf, req.ifr_ifru.ifru_hwaddr.sa_data, 6);
        err = CHIP_NO_ERROR;
    }

    close(skfd);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetInterfaceIPv4Addrs(const char * ifname, uint8_t & size, NetworkInterface * ifp)
{
    CHIP_ERROR err          = CHIP_ERROR_READ_FAILED;
    struct ifaddrs * ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
        return err;
    }

    uint8_t index = 0;
    for (struct ifaddrs * ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET)
        {
            if (strcmp(ifname, ifa->ifa_name) == 0)
            {
                void * addPtr = &((struct sockaddr_in *) ifa->ifa_addr)->sin_addr;

                memcpy(ifp->Ipv4AddressesBuffer[index], addPtr, kMaxIPv4AddrSize);
                ifp->Ipv4AddressSpans[index] = ByteSpan(ifp->Ipv4AddressesBuffer[index], kMaxIPv4AddrSize);
                index++;

                if (index >= kMaxIPv4AddrCount)
                {
                    break;
                }
            }
        }
    }

    if (index > 0)
    {
        err  = CHIP_NO_ERROR;
        size = index;
    }

    freeifaddrs(ifaddr);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetInterfaceIPv6Addrs(const char * ifname, uint8_t & size, NetworkInterface * ifp)
{
    CHIP_ERROR err          = CHIP_ERROR_READ_FAILED;
    struct ifaddrs * ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
        return err;
    }

    uint8_t index = 0;
    for (struct ifaddrs * ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6)
        {
            if (strcmp(ifname, ifa->ifa_name) == 0)
            {
                void * addPtr = &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;

                memcpy(ifp->Ipv6AddressesBuffer[index], addPtr, kMaxIPv6AddrSize);
                ifp->Ipv6AddressSpans[index] = ByteSpan(ifp->Ipv6AddressesBuffer[index], kMaxIPv6AddrSize);
                index++;

                if (index >= kMaxIPv6AddrCount)
                {
                    break;
                }
            }
        }
    }

    if (index > 0)
    {
        err  = CHIP_NO_ERROR;
        size = index;
    }

    freeifaddrs(ifaddr);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetWiFiInterfaceName(char * ifname, size_t bufSize)
{
    CHIP_ERROR err          = CHIP_ERROR_READ_FAILED;
    struct ifaddrs * ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
        return err;
    }

    struct ifaddrs * ifa = nullptr;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (GetInterfaceConnectionType(ifa->ifa_name) == InterfaceTypeEnum::kWiFi)
        {
            Platform::CopyString(ifname, bufSize, ifa->ifa_name);
            err = CHIP_NO_ERROR;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetWiFiChannelNumber(const char * ifname, uint16_t & channelNumber)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;
    struct iwreq wrq;
    int skfd;

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if ((err = GetWiFiParameter(skfd, ifname, SIOCGIWFREQ, &wrq)) == CHIP_NO_ERROR)
    {
        double freq = ConvertFrequenceToFloat(&(wrq.u.freq));
        VerifyOrReturnError((freq / 1000000) <= UINT16_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
        channelNumber = MapFrequencyToChannel(static_cast<uint16_t>(freq / 1000000));

        err = CHIP_NO_ERROR;
    }
    else
    {
        ChipLogError(DeviceLayer, "Failed to get channel/frequency (Hz).")
    }

    close(skfd);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetWiFiRssi(const char * ifname, int8_t & rssi)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;
    struct iw_statistics stats;
    int skfd;

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if ((err = GetWiFiStats(skfd, ifname, &stats)) == CHIP_NO_ERROR)
    {
        struct iw_quality * qual = &stats.qual;

        if (qual->updated & IW_QUAL_RCPI)
        {
            /* RCPI = int{(Power in dBm +110)*2} for 0dbm > Power > -110dBm */
            if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
            {
                double rcpilevel = (qual->level / 2.0) - 110.0;
                VerifyOrReturnError(rcpilevel <= INT8_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
                rssi = static_cast<int8_t>(rcpilevel);
                err  = CHIP_NO_ERROR;
            }
        }
        else
        {
            if (qual->updated & IW_QUAL_DBM)
            {
                if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
                {
                    int dblevel = qual->level;
                    /* dBm[-192; 63] */
                    if (qual->level >= 64)
                        dblevel -= 0x100;

                    VerifyOrReturnError(dblevel <= INT8_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
                    rssi = static_cast<int8_t>(dblevel);
                    err  = CHIP_NO_ERROR;
                }
            }
            else
            {
                if (!(qual->updated & IW_QUAL_LEVEL_INVALID))
                {
                    VerifyOrReturnError(qual->level <= INT8_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
                    rssi = static_cast<int8_t>(qual->level);
                    err  = CHIP_NO_ERROR;
                }
            }
        }
    }
    else
    {
        ChipLogError(DeviceLayer, "Failed to get /proc/net/wireless stats.")
    }

    close(skfd);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetWiFiBeaconLostCount(const char * ifname, uint32_t & beaconLostCount)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;
    struct iw_statistics stats;
    int skfd;

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if (GetWiFiStats(skfd, ifname, &stats) == CHIP_NO_ERROR)
    {
        beaconLostCount = stats.miss.beacon;
        err             = CHIP_NO_ERROR;
    }

    close(skfd);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetWiFiCurrentMaxRate(const char * ifname, uint64_t & currentMaxRate)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;
    struct iwreq wrq;
    int skfd;

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if ((err = GetWiFiParameter(skfd, ifname, SIOCGIWRATE, &wrq)) == CHIP_NO_ERROR)
    {
        currentMaxRate = wrq.u.bitrate.value;
        err            = CHIP_NO_ERROR;
    }
    else
    {
        ChipLogError(DeviceLayer, "Failed to get channel/frequency (Hz).")
    }

    close(skfd);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetEthInterfaceName(char * ifname, size_t bufSize)
{
    CHIP_ERROR err          = CHIP_ERROR_READ_FAILED;
    struct ifaddrs * ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
        return err;
    }

    struct ifaddrs * ifa = nullptr;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (GetInterfaceConnectionType(ifa->ifa_name) == InterfaceTypeEnum::kEthernet)
        {
            Platform::CopyString(ifname, bufSize, ifa->ifa_name);
            err = CHIP_NO_ERROR;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return err;
}

CHIP_ERROR ConnectivityUtils::GetEthPHYRate(const char * ifname, PHYRateEnum & pHYRate)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    int skfd;
    uint32_t speed          = 0;
    struct ethtool_cmd ecmd = {};
    ecmd.cmd                = ETHTOOL_GSET;
    struct ifreq ifr        = {};

    ifr.ifr_data = reinterpret_cast<char *>(&ecmd);
    Platform::CopyString(ifr.ifr_name, ifname);

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if (ioctl(skfd, SIOCETHTOOL, &ifr) == -1)
    {
        ChipLogError(DeviceLayer, "Cannot get device settings");
        close(skfd);
        return CHIP_ERROR_READ_FAILED;
    }

    speed = (ecmd.speed_hi << 16) | ecmd.speed;
    switch (speed)
    {
    case 10:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE10_M;
        break;
    case 100:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE100_M;
        break;
    case 1000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE1_G;
        break;
    case 25000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE2_5_G;
        break;
    case 5000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE5_G;
        break;
    case 10000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE10_G;
        break;
    case 40000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE40_G;
        break;
    case 100000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE100_G;
        break;
    case 200000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE200_G;
        break;
    case 400000:
        pHYRate = EmberAfPHYRateEnum::EMBER_ZCL_PHY_RATE_ENUM_RATE400_G;
        break;
    default:
        ChipLogError(DeviceLayer, "Undefined speed! (%d)\n", speed);
        err = CHIP_ERROR_READ_FAILED;
        break;
    };

    close(skfd);

    return err;
}

CHIP_ERROR ConnectivityUtils::GetEthFullDuplex(const char * ifname, bool & fullDuplex)
{
    CHIP_ERROR err = CHIP_ERROR_READ_FAILED;

    int skfd;
    struct ethtool_cmd ecmd = {};
    ecmd.cmd                = ETHTOOL_GSET;
    struct ifreq ifr        = {};

    ifr.ifr_data = reinterpret_cast<char *>(&ecmd);
    Platform::CopyString(ifr.ifr_name, ifname);

    if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ChipLogError(DeviceLayer, "Failed to create a channel to the NET kernel.");
        return CHIP_ERROR_OPEN_FAILED;
    }

    if (ioctl(skfd, SIOCETHTOOL, &ifr) == -1)
    {
        ChipLogError(DeviceLayer, "Cannot get device settings");
        err = CHIP_ERROR_READ_FAILED;
    }
    else
    {
        fullDuplex = ecmd.duplex == DUPLEX_FULL;
        err        = CHIP_NO_ERROR;
    }

    close(skfd);

    return err;
}

uint16_t ConnectivityUtils::Map2400MHz(const uint8_t inChannel)
{
    uint16_t frequency = 0;

    if (inChannel >= 1 && inChannel <= 13)
    {
        frequency = static_cast<uint16_t>(2412 + ((inChannel - 1) * 5));
    }
    else if (inChannel == 14)
    {
        frequency = 2484;
    }

    return frequency;
}

uint16_t ConnectivityUtils::Map5000MHz(const uint8_t inChannel)
{
    uint16_t frequency = 0;

    switch (inChannel)
    {
    case 183:
        frequency = 4915;
        break;
    case 184:
        frequency = 4920;
        break;
    case 185:
        frequency = 4925;
        break;
    case 187:
        frequency = 4935;
        break;
    case 188:
        frequency = 4940;
        break;
    case 189:
        frequency = 4945;
        break;
    case 192:
        frequency = 4960;
        break;
    case 196:
        frequency = 4980;
        break;
    case 7:
        frequency = 5035;
        break;
    case 8:
        frequency = 5040;
        break;
    case 9:
        frequency = 5045;
        break;
    case 11:
        frequency = 5055;
        break;
    case 12:
        frequency = 5060;
        break;
    case 16:
        frequency = 5080;
        break;
    case 34:
        frequency = 5170;
        break;
    case 36:
        frequency = 5180;
        break;
    case 38:
        frequency = 5190;
        break;
    case 40:
        frequency = 5200;
        break;
    case 42:
        frequency = 5210;
        break;
    case 44:
        frequency = 5220;
        break;
    case 46:
        frequency = 5230;
        break;
    case 48:
        frequency = 5240;
        break;
    case 52:
        frequency = 5260;
        break;
    case 56:
        frequency = 5280;
        break;
    case 60:
        frequency = 5300;
        break;
    case 64:
        frequency = 5320;
        break;
    case 100:
        frequency = 5500;
        break;
    case 104:
        frequency = 5520;
        break;
    case 108:
        frequency = 5540;
        break;
    case 112:
        frequency = 5560;
        break;
    case 116:
        frequency = 5580;
        break;
    case 120:
        frequency = 5600;
        break;
    case 124:
        frequency = 5620;
        break;
    case 128:
        frequency = 5640;
        break;
    case 132:
        frequency = 5660;
        break;
    case 136:
        frequency = 5680;
        break;
    case 140:
        frequency = 5700;
        break;
    case 149:
        frequency = 5745;
        break;
    case 153:
        frequency = 5765;
        break;
    case 157:
        frequency = 5785;
        break;
    case 161:
        frequency = 5805;
        break;
    case 165:
        frequency = 5825;
        break;
    }

    return frequency;
}

double ConnectivityUtils::ConvertFrequenceToFloat(const iw_freq * in)
{
    double result = (double) in->m;

    for (int i = 0; i < in->e; i++)
        result *= 10;

    return result;
}

CHIP_ERROR ConnectivityUtils::GetWiFiParameter(int skfd,            /* Socket to the kernel */
                                               const char * ifname, /* Device name */
                                               int request,         /* WE ID */
                                               struct iwreq * pwrq) /* Fixed part of the request */
{
    Platform::CopyString(pwrq->ifr_name, ifname);

    if (ioctl(skfd, request, pwrq) < 0)
        return CHIP_ERROR_BAD_REQUEST;

    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityUtils::GetWiFiStats(int skfd, const char * ifname, struct iw_statistics * stats)
{
    struct iwreq wrq;

    wrq.u.data.pointer = (caddr_t) stats;
    wrq.u.data.length  = sizeof(struct iw_statistics);
    wrq.u.data.flags   = 1; /*Clear updated flag */
    Platform::CopyString(wrq.ifr_name, ifname);

    return GetWiFiParameter(skfd, ifname, SIOCGIWSTATS, &wrq);
}

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
