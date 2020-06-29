#include <activationregistration.h>
#include <string>
#include <cor.h>

#include "wil/result.h"
#include "wil/resource.h"

HRESULT WinRTLoadComponent(std::wstring componentPath);

HRESULT WinRTGetThreadingModel(
    HSTRING activatableClassId,
    ABI::Windows::Foundation::ThreadingType* threading_model);

HRESULT WinRTGetActivationFactory(
    HSTRING activatableClassId,
    REFIID  iid,
    void** factory);

HRESULT WinRTGetMetadataFile(
    const HSTRING        name,
    IMetaDataDispenserEx* metaDataDispenser,
    HSTRING* metaDataFilePath,
    IMetaDataImport2** metaDataImport,
    mdTypeDef* typeDefToken);

extern std::vector<std::wstring> manifestDirs;