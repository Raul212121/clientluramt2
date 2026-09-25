#pragma once

#include <string>

class CHWIDManager
{
public:
	static CHWIDManager& Instance();

	bool Build();

	const char* GetStrictHash() const;
	const char* GetMediumHash() const;
	const char* GetSoftHash() const;

	int GetVMScore() const;
	bool IsVMDetected() const;

private:
	CHWIDManager();

	std::string m_strictHash;
	std::string m_mediumHash;
	std::string m_softHash;

	int m_vmScore;
	bool m_vmDetected;
};