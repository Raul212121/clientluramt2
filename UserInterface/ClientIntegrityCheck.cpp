#include "stdafx.h"
#include "ClientIntegrityCheck.h"
#include "HWIDManager.h"
#include <windows.h>
#include <wininet.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#include <wincrypt.h>

#pragma comment(lib, "Advapi32.lib")

#pragma comment(lib, "Wininet.lib")

static const char* PATCH_CONFIG_URL = "https://luramt2.ro/patch_config.php";

struct SManifestFile
{
	std::string path;
	DWORD size;
	std::string sha256;
};

static void ReplaceAll(std::string& str, const std::string& from, const std::string& to)
{
	if (from.empty())
		return;

	size_t startPos = 0;

	while ((startPos = str.find(from, startPos)) != std::string::npos)
	{
		str.replace(startPos, from.length(), to);
		startPos += to.length();
	}
}

static std::string Trim(const std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	size_t end = s.find_last_not_of(" \t\r\n");

	if (start == std::string::npos || end == std::string::npos)
		return "";

	return s.substr(start, end - start + 1);
}

static std::string ExtractJsonStringValue(const std::string& line)
{
	size_t colon = line.find(":");
	if (colon == std::string::npos)
		return "";

	size_t firstQuote = line.find("\"", colon);
	if (firstQuote == std::string::npos)
		return "";

	size_t secondQuote = line.find("\"", firstQuote + 1);
	if (secondQuote == std::string::npos)
		return "";

	return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

static DWORD ExtractJsonNumberValue(const std::string& line)
{
	size_t colon = line.find(":");
	if (colon == std::string::npos)
		return 0;

	std::string value = Trim(line.substr(colon + 1));
	value.erase(std::remove(value.begin(), value.end(), ','), value.end());

	return (DWORD)strtoul(value.c_str(), NULL, 10);
}

static bool DownloadUrlToString(const char* url, std::string& outData)
{
	outData.clear();

	HINTERNET hInternet = InternetOpenA(
		"LuraMT2Client",
		INTERNET_OPEN_TYPE_PRECONFIG,
		NULL,
		NULL,
		0
	);

	if (!hInternet)
		return false;

	HINTERNET hFile = InternetOpenUrlA(
		hInternet,
		url,
		NULL,
		0,
		INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
		0
	);

	if (!hFile)
	{
		InternetCloseHandle(hInternet);
		return false;
	}

	char buffer[4096];
	DWORD bytesRead = 0;

	while (InternetReadFile(hFile, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
	{
		outData.append(buffer, bytesRead);
	}

	InternetCloseHandle(hFile);
	InternetCloseHandle(hInternet);

	return !outData.empty();
}

static std::string ExtractJsonStringByKey(const std::string& json, const char* key)
{
	std::string searchKey = "\"";
	searchKey += key;
	searchKey += "\"";

	size_t keyPos = json.find(searchKey);
	if (keyPos == std::string::npos)
		return "";

	size_t colonPos = json.find(":", keyPos);
	if (colonPos == std::string::npos)
		return "";

	size_t firstQuote = json.find("\"", colonPos);
	if (firstQuote == std::string::npos)
		return "";

	size_t secondQuote = json.find("\"", firstQuote + 1);
	if (secondQuote == std::string::npos)
		return "";

	std::string value = json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
	ReplaceAll(value, "\\/", "/");
	return value;
}

static bool LoadManifestFromString(const std::string& data, std::vector<SManifestFile>& outFiles)
{
	outFiles.clear();

	std::istringstream stream(data);
	std::string line;

	SManifestFile current;
	bool inFileBlock = false;

	while (std::getline(stream, line))
	{
		line = Trim(line);

		if (line.find("\"path\"") != std::string::npos)
		{
			current = SManifestFile();
			current.path = ExtractJsonStringValue(line);
			inFileBlock = true;
		}
		else if (inFileBlock && line.find("\"size\"") != std::string::npos)
		{
			current.size = ExtractJsonNumberValue(line);
		}
		else if (inFileBlock && line.find("\"sha256\"") != std::string::npos)
		{
			current.sha256 = ExtractJsonStringValue(line);

			if (!current.path.empty() && !current.sha256.empty())
				outFiles.push_back(current);

			inFileBlock = false;
		}
	}

	return !outFiles.empty();
}

static bool GetFileSizeDWORD(const std::string& path, DWORD& outSize)
{
	HANDLE hFile = CreateFileA(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD sizeHigh = 0;
	DWORD sizeLow = GetFileSize(hFile, &sizeHigh);

	CloseHandle(hFile);

	if (sizeHigh != 0 || sizeLow == INVALID_FILE_SIZE)
		return false;

	outSize = sizeLow;
	return true;
}

static bool CalculateFileSHA256(const std::string& path, std::string& outHash)
{
	outHash.clear();

	HANDLE hFile = CreateFileA(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;

	if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
	{
		CloseHandle(hFile);
		return false;
	}

	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
	{
		CryptReleaseContext(hProv, 0);
		CloseHandle(hFile);
		return false;
	}

	BYTE buffer[64 * 1024];
	DWORD bytesRead = 0;

	while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0)
	{
		if (!CryptHashData(hHash, buffer, bytesRead, 0))
		{
			CryptDestroyHash(hHash);
			CryptReleaseContext(hProv, 0);
			CloseHandle(hFile);
			return false;
		}
	}

	BYTE hash[32];
	DWORD hashLen = sizeof(hash);

	if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0))
	{
		CryptDestroyHash(hHash);
		CryptReleaseContext(hProv, 0);
		CloseHandle(hFile);
		return false;
	}

	static const char* hex = "0123456789abcdef";

	for (DWORD i = 0; i < hashLen; ++i)
	{
		outHash.push_back(hex[(hash[i] >> 4) & 0xF]);
		outHash.push_back(hex[hash[i] & 0xF]);
	}

	CryptDestroyHash(hHash);
	CryptReleaseContext(hProv, 0);
	CloseHandle(hFile);

	return true;
}

bool RunClientIntegrityCheck()
{
	CHWIDManager::Instance().Build();

	const char* developerHWID[] =
	{
		"0a2da7bd5fb01c4d824bff0a9bbe6135445908231d9a1a18b5d81a1129ddd501",
		"3fd0b35287fec020baf47d0fe46deec931a9c0aa8da6145b5651d536493f1d65"
	};

	CHWIDManager& hwid = CHWIDManager::Instance();

	for (int i = 0; i < 2; ++i)
	{
		if (_stricmp(hwid.GetStrictHash(), developerHWID[i]) == 0 ||
			_stricmp(hwid.GetMediumHash(), developerHWID[i]) == 0 ||
			_stricmp(hwid.GetSoftHash(), developerHWID[i]) == 0)
		{
			return true;
		}
	}

	std::string configData;

	if (!DownloadUrlToString(PATCH_CONFIG_URL, configData))
	{
		MessageBoxA(NULL, "Nu pot descarca patch_config.", "LuraMT2 Update", MB_ICONERROR);
		return false;
	}

	std::string manifestUrl = ExtractJsonStringByKey(configData, "manifest_url");

	if (manifestUrl.empty())
	{
		MessageBoxA(NULL, "Nu pot citi manifest_url din patch_config.", "LuraMT2 Update", MB_ICONERROR);
		return false;
	}

	std::string manifestData;

	if (!DownloadUrlToString(manifestUrl.c_str(), manifestData))
	{
		MessageBoxA(NULL, "Nu pot descarca manifestul activ.", "LuraMT2 Update", MB_ICONERROR);
		return false;
	}

	std::vector<SManifestFile> files;

	if (!LoadManifestFromString(manifestData, files))
	{
		MessageBoxA(NULL, "Manifest invalid sau fara fisiere.", "LuraMT2 Update", MB_ICONERROR);
		return false;
	}

	int okFiles = 0;
	int missingFiles = 0;
	int sizeMismatchFiles = 0;
	int hashMismatchFiles = 0;

	for (size_t i = 0; i < files.size(); ++i)
	{
		DWORD localSize = 0;

		if (!GetFileSizeDWORD(files[i].path, localSize))
		{
			++missingFiles;
			continue;
		}

		if (localSize != files[i].size)
		{
			++sizeMismatchFiles;
			continue;
		}

		std::string localHash;

		if (!CalculateFileSHA256(files[i].path, localHash))
		{
			++hashMismatchFiles;
			continue;
		}

		if (_stricmp(localHash.c_str(), files[i].sha256.c_str()) != 0)
		{
			++hashMismatchFiles;
			continue;
		}

		++okFiles;
	}

	char msg[512];
	sprintf(
		msg,
		"Patch config OK.\n\nManifest URL:\n%.250s\n\nManifest descarcat in RAM.\nMarime: %d bytes\nFisiere gasite: %d\n\nOK: %d\nLipsa: %d\nMarime diferita: %d\nHash diferit: %d",
		manifestUrl.c_str(),
		(int)manifestData.size(),
		(int)files.size(),
		okFiles,
		missingFiles,
		sizeMismatchFiles,
		hashMismatchFiles
	);

	if (missingFiles > 0 || sizeMismatchFiles > 0 || hashMismatchFiles > 0)
	{
		char errorMsg[512];
		sprintf(
			errorMsg,
			"Client modificat sau invechit. Ruleaza AutoPatcher pentru actualizare.",
			missingFiles,
			sizeMismatchFiles,
			hashMismatchFiles
		);

		MessageBoxA(NULL, errorMsg, "LuraMT2 Update", MB_ICONERROR);

		configData.clear();
		manifestData.clear();
		files.clear();

		return false;
	}

	//MessageBoxA(NULL, msg, "LuraMT2 Update", MB_OK);

	configData.clear();
	manifestData.clear();
	files.clear();

	return true;
}