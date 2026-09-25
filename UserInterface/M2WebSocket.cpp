#include "StdAfx.h"
#ifdef ENABLE_VOTE4BUFF
#include <iostream>
#include "M2WebSocket.h"
#include <Shellapi.h>

SimpleHttpClient::SimpleHttpClient()
{
}

SimpleHttpClient::~SimpleHttpClient()
{
    Cancel();
}

void SimpleHttpClient::Request(const std::string& url)
{
    Cancel();

    m_Completed = false;
    m_Response.clear();

    m_RequestThread = std::thread(&SimpleHttpClient::PerformRequest, this, url);
    m_RequestThread.detach();
}

void SimpleHttpClient::Cancel()
{
    if (m_RequestThread.joinable())
        m_RequestThread.join();
}

static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* response = reinterpret_cast<std::string*>(userdata);
    response->append(ptr, size * nmemb);
    return size * nmemb;
}

static int CurlDebugCallback(void* handle, int type, char* data, size_t size, void* userptr)
{
    switch (type)
    {
        case 0: // CURLINFO_TEXT
        case 1: // CURLINFO_HEADER_IN
        case 2: // CURLINFO_HEADER_OUT
        case 3: // CURLINFO_DATA_IN
        case 4: // CURLINFO_DATA_OUT
            TraceErrorWithoutEnter("[curl] %.*s", (int)size, data);
            break;
        default:
            break;
    }

    return 0;
}

void SimpleHttpClient::PerformRequest(const std::string& url)
{
    HMODULE hCurl = LoadLibraryA("libcurl.dll");
    if (!hCurl)
    {
        LogBox("libcurl.dll not found");
        m_Response = "libcurl.dll not found";
        m_Completed = true;
        return;
    }

    // Define curl function pointer types
    using curl_easy_init_t = void* (__cdecl*)();
    using curl_easy_setopt_t = int(__cdecl*)(void*, int, ...);
    using curl_easy_perform_t = int(__cdecl*)(void*);
    using curl_easy_cleanup_t = void(__cdecl*)(void*);
    using curl_easy_strerror_t = const char* (__cdecl*)(int);
    using curl_global_init_t = int(__cdecl*)(long);
    using curl_global_cleanup_t = void(__cdecl*)();

    // Define needed curl constants
    constexpr int CURLOPT_URL = 10002;
    constexpr int CURLOPT_WRITEFUNCTION = 20011;
    constexpr int CURLOPT_WRITEDATA = 10001;
    constexpr int CURLOPT_TIMEOUT_MS = 155;
    constexpr int CURLOPT_FOLLOWLOCATION = 52;
    constexpr int CURLOPT_VERBOSE = 41;
    constexpr int CURLOPT_DEBUGFUNCTION = 20094;
    constexpr int CURLOPT_DEBUGDATA = 10095;
    constexpr int CURLOPT_FORBID_REUSE = 75;
    constexpr long CURL_GLOBAL_DEFAULT = (1 << 0);
    constexpr int CURLE_OK = 0;

    // Get curl function pointers
    auto curl_global_init = (curl_global_init_t)GetProcAddress(hCurl, "curl_global_init");
    auto curl_global_cleanup = (curl_global_cleanup_t)GetProcAddress(hCurl, "curl_global_cleanup");
    auto curl_easy_init = (curl_easy_init_t)GetProcAddress(hCurl, "curl_easy_init");
    auto curl_easy_setopt = (curl_easy_setopt_t)GetProcAddress(hCurl, "curl_easy_setopt");
    auto curl_easy_perform = (curl_easy_perform_t)GetProcAddress(hCurl, "curl_easy_perform");
    auto curl_easy_cleanup = (curl_easy_cleanup_t)GetProcAddress(hCurl, "curl_easy_cleanup");
    auto curl_easy_strerror = (curl_easy_strerror_t)GetProcAddress(hCurl, "curl_easy_strerror");

    if (!curl_global_init || !curl_easy_init || !curl_easy_setopt || !curl_easy_perform ||
        !curl_easy_cleanup || !curl_easy_strerror || !curl_global_cleanup)
    {
        LogBox("libcurl.dll is missing required functions");
        m_Response = "libcurl.dll is missing required functions";
        m_Completed = true;
        FreeLibrary(hCurl);
        return;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    void* curl = curl_easy_init();
    if (!curl)
    {
        m_Response = "Failed to initialize curl.";
        m_Completed = true;
        curl_global_cleanup();
        FreeLibrary(hCurl);
        return;
    }

    std::string responseStr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, m_Timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
#ifdef _DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, CurlDebugCallback);
#endif

    const int res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        m_Response = std::string("Curl error: ") + curl_easy_strerror(res);
    }
    else
    {
        m_Response = responseStr;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    FreeLibrary(hCurl);

    m_Completed = true;
}

bool PyTuple_GetSocket(PyObject* args, int pos, SimpleHttpClient** retSocket)
{
    int handle;
    if (!PyTuple_GetInteger(args, pos, &handle))
        return false;
    if (!handle)
        return false;

    *retSocket = (SimpleHttpClient*)handle;
    return true;
}

PyObject* m2webRequest(PyObject* poSelf, PyObject* poArgs)
{
    char* url;
    if (!PyTuple_GetString(poArgs, 0, &url))
        return Py_BadArgument();
    
    int timeout = 5000;
    if (PyTuple_Size(poArgs) > 1 && !PyTuple_GetInteger(poArgs, 1, &timeout))
        return Py_BadArgument();

    SimpleHttpClient* socket = new SimpleHttpClient();
    socket->SetTimeout(timeout);
    socket->Request(url);

    return Py_BuildValue("i", socket);
}

PyObject* m2webIsCompleted(PyObject* poSelf, PyObject* poArgs)
{
    SimpleHttpClient* socket;
    if (!PyTuple_GetSocket(poArgs, 0, &socket))
        return Py_BadArgument();

    return Py_BuildValue("b", socket->IsCompleted());
}

PyObject* m2webGetResponse(PyObject* poSelf, PyObject* poArgs)
{
    SimpleHttpClient* socket;
    if (!PyTuple_GetSocket(poArgs, 0, &socket))
        return Py_BadArgument();

    return Py_BuildValue("s", socket->GetResponse().c_str());
}

PyObject* m2webDestroy(PyObject* poSelf, PyObject* poArgs)
{
    SimpleHttpClient* socket;
    if (!PyTuple_GetSocket(poArgs, 0, &socket))
        return Py_BadArgument();

    delete socket;
    return Py_BuildNone();
}

PyObject* m2webOpenExternalBrowser(PyObject* poSelf, PyObject* poArgs)
{
    char* url;
    if (!PyTuple_GetString(poArgs, 0, &url))
        return Py_BadArgument();

    ShellExecuteA(0, "open", url, 0, 0, SW_HIDE);
    return Py_BuildNone();
}

void initM2WebSocket()
{
    static PyMethodDef s_methods[] =
    {
        { "Request",                m2webRequest,                   METH_VARARGS },
        { "IsCompleted",            m2webIsCompleted,               METH_VARARGS },
        { "GetResponse",            m2webGetResponse,               METH_VARARGS },
        { "Destroy",                m2webDestroy,                   METH_VARARGS },
        { "OpenExternalBrowser",    m2webOpenExternalBrowser,       METH_VARARGS },

        { NULL,                     NULL,                           NULL },
    };

    PyObject* poModule = Py_InitModule("m2web", s_methods);
}
#endif
