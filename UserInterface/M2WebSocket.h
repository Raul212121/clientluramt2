#pragma once

#include "StdAfx.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

class SimpleHttpClient
{
	public:
        SimpleHttpClient();
        ~SimpleHttpClient();

        void Request(const std::string& url);
		void PerformRequest(const std::string& url);
		void Cancel();

		void SetTimeout(uint32_t milliseconds)	{ m_Timeout = milliseconds; }
        bool IsCompleted() const				{ return m_Completed; }
        std::string GetResponse() const			{ return m_Response; }

	private:
		std::thread			m_RequestThread;
		std::atomic<bool>	m_Completed{ false };
		std::atomic<bool>	m_Cancelled{ false };
		std::string			m_Response{ "" };
		uint32_t			m_Timeout{ 5000 };
};

void initM2WebSocket();
