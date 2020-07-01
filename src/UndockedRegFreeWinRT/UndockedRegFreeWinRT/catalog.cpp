#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 

#include <Windows.h>
#include <roapi.h>
#include <winstring.h>
#include <rometadataresolution.h>
#include <combaseapi.h>
#include <wrl.h>
#include <xmllite.h>
#include <Shlwapi.h>
#include <comutil.h>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <codecvt>
#include <locale>
#include <filesystem>
#include "catalog.h"

using namespace std;
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

// TODO: Components won't respect COM lifetime. workaround to get them in the COM list?

extern "C"
{
    typedef HRESULT(__stdcall* activation_factory_type)(HSTRING, IActivationFactory**);
}

// Intentionally no class factory cache here. That would be excessive since 
// other layers already cache.
struct component
{
    wstring module_name;
    wstring xmlns;
    HMODULE handle = nullptr;
    activation_factory_type get_activation_factory;
    ABI::Windows::Foundation::ThreadingType threading_model;

    ~component()
    {
        if (handle)
        {
            FreeLibrary(handle);
        }
    }

    HRESULT LoadModule()
    {
        if (handle == nullptr)
        {
            handle = LoadLibraryExW(module_name.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (handle == nullptr)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            this->get_activation_factory = (activation_factory_type)GetProcAddress(handle, "DllGetActivationFactory");
            if (this->get_activation_factory == nullptr)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }
        return (handle != nullptr && this->get_activation_factory != nullptr) ? S_OK : E_FAIL;
    }

    HRESULT GetActivationFactory(HSTRING className, REFIID  iid, void** factory)
    {
        RETURN_IF_FAILED(LoadModule());

        IActivationFactory* ifactory = nullptr;
        HRESULT hr = this->get_activation_factory(className, &ifactory);
        // optimize for IActivationFactory?
        if (SUCCEEDED(hr))
        {
            hr = ifactory->QueryInterface(iid, factory);
            ifactory->Release();
        }
        return hr;
    }
};

static unordered_map < wstring, shared_ptr<component> > g_types;

HRESULT WinRTLoadComponent(wstring manifest_path)
{
    ComPtr<IStream> fileStream;
    ComPtr<IXmlReader> xmlReader;
    XmlNodeType nodeType;
    const WCHAR* localName = L"";
    auto locale = _create_locale(LC_ALL, "C");

    RETURN_IF_FAILED(SHCreateStreamOnFileEx(manifest_path.c_str(), STGM_READ, FILE_ATTRIBUTE_NORMAL, false, nullptr, &fileStream));
    RETURN_IF_FAILED(CreateXmlReader(__uuidof(IXmlReader), (void**)&xmlReader, nullptr));
    RETURN_IF_FAILED(xmlReader->SetInput(fileStream.Get()));
    filesystem::path manifestDir{manifest_path};
    manifestDir.remove_filename();

    const WCHAR* fileName;
    while (S_OK == xmlReader->Read(&nodeType))
    {
        if (nodeType == XmlNodeType_Element)
        {
            RETURN_IF_FAILED((xmlReader->GetLocalName(&localName, nullptr)));
            if (_wcsicmp_l(localName, L"file", locale) == 0)
            {
                RETURN_IF_FAILED(xmlReader->MoveToAttributeByName(L"name", nullptr));
                RETURN_IF_FAILED(xmlReader->GetValue(&fileName, nullptr));

                while (S_OK == xmlReader->Read(&nodeType))
                {
                    if (nodeType == XmlNodeType_Element)
                    {
                        RETURN_IF_FAILED((xmlReader->GetLocalName(&localName, nullptr)));
                        if (_wcsicmp_l(localName, L"activatableClass", locale) == 0)
                        {
                            auto this_component = make_shared<component>();
                            filesystem::path moduleName{manifestDir};
                            moduleName /= fileName;
                            this_component->module_name = moduleName.wstring();

                            const WCHAR* threadingModel;
                            RETURN_IF_FAILED(xmlReader->MoveToAttributeByName(L"threadingModel", nullptr));
                            RETURN_IF_FAILED(xmlReader->GetValue(&threadingModel, nullptr));

                            if (_wcsicmp_l(L"sta", threadingModel, locale) == 0)
                            {
                                this_component->threading_model = ABI::Windows::Foundation::ThreadingType::ThreadingType_STA;
                            }
                            else if (_wcsicmp_l(L"mta", threadingModel, locale) == 0)
                            {
                                this_component->threading_model = ABI::Windows::Foundation::ThreadingType::ThreadingType_MTA;
                            }
                            else if (_wcsicmp_l(L"both", threadingModel, locale) == 0)
                            {
                                this_component->threading_model = ABI::Windows::Foundation::ThreadingType::ThreadingType_BOTH;
                            }
                            else
                            {
                                return HRESULT_FROM_WIN32(ERROR_SXS_MANIFEST_PARSE_ERROR);
                            }

                            const WCHAR* xmlns;
                            RETURN_IF_FAILED(xmlReader->MoveToAttributeByName(L"xmlns", nullptr));
                            RETURN_IF_FAILED(xmlReader->GetValue(&xmlns, nullptr));
                            this_component->xmlns = xmlns;

                            const WCHAR* activatableClass;
                            RETURN_IF_FAILED(xmlReader->MoveToAttributeByName(L"clsid", nullptr));
                            RETURN_IF_FAILED(xmlReader->GetValue(&activatableClass, nullptr));
                            g_types[activatableClass] = this_component;
                        }
                    }
                }
            }
        }
    }
    wstring td{static_cast<wstring>(manifestDir)};
    td.pop_back();
    manifestDirs.push_back(td);
    return S_OK;
}

HRESULT WinRTGetThreadingModel(HSTRING activatableClassId, ABI::Windows::Foundation::ThreadingType* threading_model)
{
    auto raw_class_name = WindowsGetStringRawBuffer(activatableClassId, nullptr);
    auto component_iter = g_types.find(raw_class_name);
    if (component_iter != g_types.end())
    {
        *threading_model = component_iter->second->threading_model;
        return S_OK;
    }
    return REGDB_E_CLASSNOTREG;
}

HRESULT WinRTGetActivationFactory(
    HSTRING activatableClassId,
    REFIID  iid,
    void** factory)
{
    auto raw_class_name = WindowsGetStringRawBuffer(activatableClassId, nullptr);
    auto component_iter = g_types.find(raw_class_name);
    if (component_iter != g_types.end())
    {
        return component_iter->second->GetActivationFactory(activatableClassId, iid, factory);
    }
    return REGDB_E_CLASSNOTREG;    
}

HRESULT WinRTGetMetadataFile(
    const HSTRING        name,
    IMetaDataDispenserEx* metaDataDispenser,
    HSTRING* metaDataFilePath,
    IMetaDataImport2** metaDataImport,
    mdTypeDef* typeDefToken
)
{
    if (metaDataDispenser != nullptr ||
        metaDataImport != nullptr ||
        typeDefToken != nullptr)
    {
        return E_NOTIMPL;
    }

    wchar_t szFolderPath[9];
    HRESULT hr = StringCchCopyW(szFolderPath, _countof(szFolderPath), WindowsGetStringRawBuffer(name, nullptr));
    if (hr != S_OK && hr != STRSAFE_E_INSUFFICIENT_BUFFER)
    {
        return hr;
    }
    if (CompareStringOrdinal(szFolderPath, -1, L"Windows.", -1, false) == CSTR_EQUAL)
    {
        return REGDB_E_CLASSNOTREG;
    }
    
    wil::unique_cotaskmem_array_ptr<wil::unique_hstring> metaDataFilePaths;
    hr = S_OK;
    for (auto& dir : manifestDirs) {
        hr = RoResolveNamespace(name, HStringReference(dir.c_str()).Get(),
            0, nullptr,
            metaDataFilePaths.size_address<DWORD>(), &metaDataFilePaths,
            0, nullptr);
        if (SUCCEEDED(hr)) {
            break;
        }
    }
    RETURN_IF_FAILED(hr);
    DWORD bestMatch = 0;
    int bestMatchLength = 0;
    for (DWORD i = 0; i < metaDataFilePaths.size(); i++)
    {
        int length = WindowsGetStringLen(metaDataFilePaths[i]);
        if (length > bestMatchLength)
        {
            bestMatch = i;
            bestMatchLength = length;
        }
    }
    *metaDataFilePath = metaDataFilePaths[bestMatch];
    metaDataFilePaths[bestMatch] = nullptr; // Null this out to transfer ownership to parameter
    return S_OK;
}