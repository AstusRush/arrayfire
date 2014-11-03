/*******************************************************
 * Copyright (c) 2014, ArrayFire
 * All rights reserved.
 *
 * This file is distributed under 3-clause BSD license.
 * The complete license agreement can be obtained at:
 * http://arrayfire.com/licenses/BSD-3-Clause
 ********************************************************/

#include <af/version.h>
#include <platform.hpp>
#include <driver.h>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstdio>

//Macro for checking cuda errors following a cuda launch or api call
#define CUDA(call)                                                  \
do {                                                                \
    cudaError_t err = call;                                         \
    if (cudaSuccess != err) {                                       \
        fprintf (stderr, "CUDA Error in %s:%d : %s.",               \
                 __FILE__, __LINE__, cudaGetErrorString(err));      \
        exit(EXIT_FAILURE);                                         \
    }                                                               \
} while (0);                                                        \

using namespace std;

namespace cuda
{
    ///////////////////////////////////////////////////////////////////////////
    // HELPERS
    ///////////////////////////////////////////////////////////////////////////
    // pulled from CUTIL from CUDA SDK
    static inline int compute2cores(int major, int minor)
    {
        struct {
            int compute; // 0xMm (hex), M = major version, m = minor version
            int cores;
        } gpus[] = {
            { 0x10,  8 },
            { 0x11,  8 },
            { 0x12,  8 },
            { 0x13,  8 },
            { 0x20, 32 },
            { 0x21, 48 },
            { 0x30, 192 },
            { 0x35, 192 },
            { 0x50, 128 },
            {   -1, -1  },
        };

        for (int i = 0; gpus[i].compute != -1; ++i) {
            if (gpus[i].compute == (major << 4) + minor)
                return gpus[i].cores;
        }
        return 0;
    }

    // compare two cards based on (in order):
    //   1. flops (theoretical)
    //   2. total memory

    #define COMPARE(a,b,f) do {                     \
            return ((a)->f >= (b)->f);              \
        } while (0);


    static inline bool card_compare_compute(const cudaDevice_t &l, const cudaDevice_t &r)
    {
        const cudaDevice_t *lc = &l;
        const cudaDevice_t *rc = &r;

        COMPARE(lc, rc, prop.major);
        COMPARE(lc, rc, prop.minor);
        COMPARE(lc, rc, flops);
        COMPARE(lc, rc, prop.totalGlobalMem);
        COMPARE(lc, rc, nativeId);
        return 0;
    }

    static inline bool card_compare_flops(const cudaDevice_t &l, const cudaDevice_t &r)
    {
        const cudaDevice_t *lc = &l;
        const cudaDevice_t *rc = &r;

        COMPARE(lc, rc, flops);
        COMPARE(lc, rc, prop.totalGlobalMem);
        COMPARE(lc, rc, prop.major);
        COMPARE(lc, rc, prop.minor);
        COMPARE(lc, rc, nativeId);
        return 0;
    }

    static inline bool card_compare_mem(const cudaDevice_t &l, const cudaDevice_t &r)
    {
        const cudaDevice_t *lc = &l;
        const cudaDevice_t *rc = &r;

        COMPARE(lc, rc, prop.totalGlobalMem);
        COMPARE(lc, rc, flops);
        COMPARE(lc, rc, prop.major);
        COMPARE(lc, rc, prop.minor);
        COMPARE(lc, rc, nativeId);
        return 0;
    }

    static inline bool card_compare_num(const cudaDevice_t &l, const cudaDevice_t &r)
    {
        const cudaDevice_t *lc = &l;
        const cudaDevice_t *rc = &r;

        COMPARE(lc, rc, nativeId);
        return 0;
    }

    static const char *get_system(void)
    {
        return
    #if defined(ARCH_32)
        "32-bit "
    #elif defined(ARCH_64)
        "64-bit "
    #endif

    #if defined(OS_LNX)
        "Linux";
    #elif defined(OS_WIN)
        "Windows";
    #elif defined(OS_MAC)
        "Mac OSX";
    #endif
    }

    template <typename T>
    static inline string toString(T val)
    {
        stringstream s;
        s << val;
        return s.str();
    }

    ///////////////////////////////////////////////////////////////////////////
    // Wrapper Functions
    ///////////////////////////////////////////////////////////////////////////
    string getInfo()
    {
        ostringstream info;
        info << "ArrayFire v" << AF_VERSION << AF_VERSION_MINOR
             << " (CUDA, " << get_system() << ", build " << REVISION << ")" << std::endl;
        info << getPlatformInfo();
        for (int i = 0; i < getDeviceCount(); ++i) {
            info << getDeviceInfo(i);
        }
        return info.str();
    }

    string getDeviceInfo(int device)
    {
        cudaDeviceProp dev = getDeviceProp(device);

        size_t mem_gpu_total = dev.totalGlobalMem;
        //double cc = double(dev.major) + double(dev.minor) / 10;

        bool show_braces = getActiveDeviceId() == device;

        string id = (show_braces ? string("[") : "-") + toString(device) +
                    (show_braces ? string("]") : "-");
        string name(dev.name);
        string memory = toString((mem_gpu_total / (1024 * 1024))
                              + !!(mem_gpu_total % (1024 * 1024)))
                        + string(" MB");
        string compute = string("CUDA Compute ") + toString(dev.major) + string(".") + toString(dev.minor);

        string info = id + string(" ")  +
                    name + string(", ") +
                  memory + string(", ") +
                 compute + string("\n");
        return info;
    }

    string getPlatformInfo()
    {
        string driverVersion = getDriverVersion();
        std::string cudaRuntime = getCUDARuntimeVersion();
        string platform = "Platform: CUDA Toolkit " + cudaRuntime;
        if (!driverVersion.empty()) {
            platform.append(", Driver: ");
            platform.append(driverVersion);
        }
        platform.append("\n");
        return platform;
    }

    string getDriverVersion()
    {
        char driverVersion[1024] = {" ",};
        int x = nvDriverVersion(driverVersion, sizeof(driverVersion));
        if (x != 1) {
            #if !defined(OS_MAC) && !defined(__arm__)  // HACK Mac OSX 10.7 needs new method for fetching driver
            throw runtime_error("Invalid driver");
            #endif
            int driver = 0;
            CUDA(cudaDriverGetVersion(&driver));
            return string("CUDA Driver Version: ") + toString(driver);
        } else {
            return string(driverVersion);
        }
    }

    string getCUDARuntimeVersion()
    {
        int runtime = 0;
        CUDA(cudaRuntimeGetVersion(&runtime));
        if(runtime / 100.f > 0)
            return toString((runtime / 1000) + (runtime % 1000)/ 100.);
        else
            return toString(runtime / 1000) + string(".0");

    }

    int getDeviceCount()
    {
        return DeviceManager::getInstance().nDevices;
    }

    int getActiveDeviceId()
    {
        return DeviceManager::getInstance().activeDev;
    }

    int getDeviceNativeId(int device)
    {
        if(device < (int)DeviceManager::getInstance().cuDevices.size())
            return DeviceManager::getInstance().cuDevices[device].nativeId;
        return -1;
    }

    int setDevice(int device)
    {
        return DeviceManager::getInstance().setActiveDevice(device);
    }

    cudaDeviceProp getDeviceProp(int device)
    {
        if(device < (int)DeviceManager::getInstance().cuDevices.size())
            return DeviceManager::getInstance().cuDevices[device].prop;
        return DeviceManager::getInstance().cuDevices[0].prop;
    }

    ///////////////////////////////////////////////////////////////////////////
    // DeviceManager Class Functions
    ///////////////////////////////////////////////////////////////////////////
    DeviceManager& DeviceManager::getInstance()
    {
        static DeviceManager my_instance;
        return my_instance;
    }

    DeviceManager::DeviceManager()
        : cuDevices(0), activeDev(0), nDevices(0)
    {
        CUDA(cudaGetDeviceCount(&nDevices));
        if (nDevices == 0)
            throw runtime_error("No CUDA-Capable devices found");

        cuDevices.reserve(nDevices);

        for(int i = 0; i < nDevices; i++) {
            cudaDevice_t dev;
            cudaGetDeviceProperties(&dev.prop, i);
            dev.flops = dev.prop.multiProcessorCount * compute2cores(dev.prop.major, dev.prop.minor) * dev.prop.clockRate;
            dev.nativeId = i;
            cuDevices.push_back(dev);
        }

        sortDevices();

        setActiveDevice(0);
    }

    void DeviceManager::sortDevices(sort_mode mode)
    {
        switch(mode) {
            case memory :
                sort(cuDevices.begin(), cuDevices.end(), card_compare_mem);
                break;
            case flops :
                sort(cuDevices.begin(), cuDevices.end(), card_compare_flops);
                break;
            case compute :
                sort(cuDevices.begin(), cuDevices.end(), card_compare_compute);
                break;
            case none : default :
                sort(cuDevices.begin(), cuDevices.end(), card_compare_num);
                break;
        }
    }

    int DeviceManager::setActiveDevice(int device)
    {
        if(device > (int)cuDevices.size()) {
            return -1;
        } else {
            int old = activeDev;
            CUDA(cudaSetDevice(device));
            activeDev = device;
            return old;
        }
    }
}