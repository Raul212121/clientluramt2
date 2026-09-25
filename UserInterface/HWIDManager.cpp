#include "StdAfx.h"
#include "HWIDManager.h"

#include <windows.h>
#include <wincrypt.h>
#include <intrin.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "Advapi32.lib")

static std::string ToLowerString(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), ::tolower);
	return value;
}

static std::string ReadRegistryString(HKEY root, const char* subKey, const char* valueName)
{
	HKEY hKey;
	char buffer[512];
	DWORD bufferSize = sizeof(buffer);
	DWORD type = REG_SZ;

	if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return "";

	LONG result = RegQueryValueExA(
		hKey,
		valueName,
		0,
		&type,
		reinterpret_cast<LPBYTE>(buffer),
		&bufferSize
	);

	RegCloseKey(hKey);

	if (result != ERROR_SUCCESS || type != REG_SZ)
		return "";

	buffer[sizeof(buffer) - 1] = '\0';
	return std::string(buffer);
}

static std::string GetMachineGuid()
{
	return ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		"SOFTWARE\\Microsoft\\Cryptography",
		"MachineGuid"
	);
}

static std::string GetComputerNameValue()
{
	char buffer[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD size = sizeof(buffer);

	if (!GetComputerNameA(buffer, &size))
		return "";

	return std::string(buffer);
}

static std::string GetUserNameValue()
{
	char buffer[256];
	DWORD size = sizeof(buffer);

	if (!GetUserNameA(buffer, &size))
		return "";

	return std::string(buffer);
}

static std::string GetWindowsDirectoryValue()
{
	char buffer[MAX_PATH];

	if (!GetWindowsDirectoryA(buffer, MAX_PATH))
		return "";

	return std::string(buffer);
}

static std::string GetSystemDriveSerial()
{
	DWORD serialNumber = 0;

	if (!GetVolumeInformationA(
		"C:\\",
		NULL,
		0,
		&serialNumber,
		NULL,
		NULL,
		NULL,
		0
	))
	{
		return "";
	}

	std::ostringstream stream;
	stream << std::hex << serialNumber;
	return stream.str();
}

static std::string GetCpuInfo()
{
	int cpuInfo[4] = { 0 };
	char cpuVendor[13] = { 0 };
	char cpuBrand[49] = { 0 };

	__cpuid(cpuInfo, 0);

	memcpy(cpuVendor + 0, &cpuInfo[1], 4);
	memcpy(cpuVendor + 4, &cpuInfo[3], 4);
	memcpy(cpuVendor + 8, &cpuInfo[2], 4);
	cpuVendor[12] = '\0';

	int extendedIds = 0;
	__cpuid(cpuInfo, 0x80000000);
	extendedIds = cpuInfo[0];

	if (extendedIds >= 0x80000004)
	{
		__cpuid((int*)(cpuBrand + 0), 0x80000002);
		__cpuid((int*)(cpuBrand + 16), 0x80000003);
		__cpuid((int*)(cpuBrand + 32), 0x80000004);
		cpuBrand[48] = '\0';
	}

	std::ostringstream stream;
	stream << cpuVendor << "_" << cpuBrand;

	return stream.str();
}

static std::string Sha256String(const std::string& input)
{
	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;
	BYTE hash[32];
	DWORD hashLen = 32;

	if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
	{
		if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
			return "0000000000000000000000000000000000000000000000000000000000000000";
	}

	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
	{
		CryptReleaseContext(hProv, 0);
		return "0000000000000000000000000000000000000000000000000000000000000000";
	}

	if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.c_str()), (DWORD)input.size(), 0))
	{
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);
		return "0000000000000000000000000000000000000000000000000000000000000000";
	}

	if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
	{
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);
		return "0000000000000000000000000000000000000000000000000000000000000000";
	}

	CryptDestroyHash(hHash);
	CryptReleaseContext(hProv, 0);

	std::ostringstream stream;

	for (DWORD i = 0; i < hashLen; ++i)
	{
		stream << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	}

	return stream.str();
}

static int DetectVMScore()
{
	int score = 0;

	std::string biosVendor = ToLowerString(ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\BIOS",
		"BIOSVendor"
	));

	std::string systemManufacturer = ToLowerString(ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\BIOS",
		"SystemManufacturer"
	));

	std::string systemProduct = ToLowerString(ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		"HARDWARE\\DESCRIPTION\\System\\BIOS",
		"SystemProductName"
	));

	std::string all = biosVendor + " " + systemManufacturer + " " + systemProduct;

	if (all.find("virtualbox") != std::string::npos)
		score += 80;

	if (all.find("vmware") != std::string::npos)
		score += 80;

	if (all.find("qemu") != std::string::npos)
		score += 80;

	if (all.find("kvm") != std::string::npos)
		score += 80;

	if (all.find("hyper-v") != std::string::npos)
		score += 60;

	if (all.find("virtual") != std::string::npos)
		score += 50;

	return score;
}

CHWIDManager& CHWIDManager::Instance()
{
	static CHWIDManager s_instance;
	return s_instance;
}

CHWIDManager::CHWIDManager()
{
	m_strictHash = "0000000000000000000000000000000000000000000000000000000000000000";
	m_mediumHash = "0000000000000000000000000000000000000000000000000000000000000000";
	m_softHash = "0000000000000000000000000000000000000000000000000000000000000000";
	m_vmScore = 0;
	m_vmDetected = false;
}

bool CHWIDManager::Build()
{
	std::string machineGuid = GetMachineGuid();
	std::string volumeSerial = GetSystemDriveSerial();
	std::string cpuInfo = GetCpuInfo();
	std::string computerName = GetComputerNameValue();
	std::string userName = GetUserNameValue();
	std::string windowsDir = GetWindowsDirectoryValue();

	std::string strictData =
		"strict|" + machineGuid + "|" + volumeSerial + "|" + cpuInfo + "|" + computerName + "|" + userName + "|" + windowsDir;

	std::string mediumData =
		"medium|" + machineGuid + "|" + volumeSerial + "|" + cpuInfo;

	std::string softData =
		"soft|" + machineGuid + "|" + volumeSerial;

	m_strictHash = Sha256String(strictData);
	m_mediumHash = Sha256String(mediumData);
	m_softHash = Sha256String(softData);

	m_vmScore = DetectVMScore();
	m_vmDetected = m_vmScore >= 80;

	return true;
}

const char* CHWIDManager::GetStrictHash() const
{
	return m_strictHash.c_str();
}

const char* CHWIDManager::GetMediumHash() const
{
	return m_mediumHash.c_str();
}

const char* CHWIDManager::GetSoftHash() const
{
	return m_softHash.c_str();
}

int CHWIDManager::GetVMScore() const
{
	return m_vmScore;
}

bool CHWIDManager::IsVMDetected() const
{
	return m_vmDetected;
}