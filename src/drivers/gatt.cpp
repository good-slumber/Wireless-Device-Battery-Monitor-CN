// Active GATT battery read for paired Bluetooth LE phones (e.g. iPhone).
//
// Windows caches the LE battery level it reads over the GATT Battery Service (180F / 2A19) in a
// device property that Settings > Bluetooth & devices shows. That cache is refreshed by the system
// Bluetooth service on its own slow schedule (often tens of minutes behind once the phone stops
// notifying), so PollBluetooth can report a stale percentage even though the phone has charged since.
//
// This driver talks to the device directly through the WinRT Bluetooth GATT projection, reads the
// 2A19 characteristic on demand and returns the live percentage. A direct read also refreshes the
// system cache, so the Settings page catches up afterwards too. Anything that goes wrong (phone out
// of range, unreachable, no battery service) returns -1 and the caller keeps the cached value.

#include "drivers.h"

#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>

// ---- minimal WinRT ABI declarations ------------------------------------------------------------
// Vtable order from the Windows SDK headers windows.devices.bluetooth(.genericattributeprofile).h.
// Deliberately NOT in an anonymous namespace: GCC would see interfaces nobody in the program can
// implement and turn every call through them into a trap (-fdevirtualize, at -O1 and above).
namespace gatt_abi {

const GUID IID_BLEDeviceStatics = { 0xc8cf1a19, 0xf0b6, 0x4bf0, { 0x86, 0x89, 0x41, 0x30, 0x3d, 0xe2, 0xd9, 0xf4 } };
const GUID IID_BLEDevice = { 0xb5ee2f7b, 0x4ad8, 0x4642, { 0xac, 0x48, 0x80, 0xa0, 0xb5, 0x00, 0xe8, 0x87 } };
const GUID IID_GattDeviceService3 = { 0xb293a950, 0x0c53, 0x437c, { 0xa9, 0xb3, 0x5c, 0x32, 0x10, 0xc6, 0xe5, 0x69 } };
const GUID IID_GattCharacteristicsResult = { 0x1194945c, 0xb257, 0x4f3e, { 0x9d, 0xb7, 0xf6, 0x8b, 0xc9, 0xa9, 0xae, 0xf2 } };
const GUID IID_VectorView_GattCharacteristic = { 0xcb3ab3ae, 0xb561, 0x504f, { 0xa8, 0x08, 0x59, 0x9d, 0xec, 0xeb, 0x2d, 0xf4 } };
const GUID IID_GattCharacteristic = { 0x59cb50c1, 0x5934, 0x4f68, { 0xa1, 0x98, 0xeb, 0x86, 0x4f, 0xa4, 0x4e, 0x6b } };
const GUID IID_GattReadResult = { 0x63a66f08, 0x1aea, 0x4c4c, { 0xa5, 0x0f, 0x97, 0xba, 0xe4, 0x74, 0xb3, 0x48 } };
const GUID IID_BufferByteAccess = { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
const GUID IID_IBuffer = { 0x905a0fe0, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };

// Primary IAsyncOperation<T> vtable: IUnknown(3) + IInspectable(3) + put_Completed / get_Completed
// / GetResults. IAsyncInfo is a separate QI interface.
struct IAsyncOpAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void** results) = 0;
};

// BluetoothLEDeviceStatics: FromIdAsync / FromBluetoothAddressAsync / GetDeviceSelector.
struct IBLEDeviceStaticsAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE FromIdAsync(HSTRING id, void** async) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromBluetoothAddressAsync(UINT64 addr, void** async) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelector(HSTRING* result) = 0;
};

// BluetoothLEDevice; only GetGattService is used, but the vtable slots before it are reserved in order.
struct IBLEDeviceAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DeviceId(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Name(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_GattServices(void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ConnectionStatus(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BluetoothAddress(UINT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattService(GUID uuid, void** service) = 0;
};

// IGattDeviceService3; GetCharacteristicsAsync is the 6th slot after IInspectable.
struct IGattService3Abi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DeviceAccessInformation(void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Session(void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SharingMode(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(void** async) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenAsync(INT32 mode, void** async) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharacteristicsAsync(void** async) = 0;
};

// IGattCharacteristicsResult: get_Status / get_ProtocolError / get_Characteristics.
struct IGattCharsResultAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ProtocolError(void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Characteristics(void** view) = 0;
};

// IVectorView<GattCharacteristic>.
struct IGattCharViewAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(void* element, UINT32* index, BOOLEAN* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 size, void** items, UINT32* v) = 0;
};

// IGattCharacteristic; ReadValueAsync is the 9th slot after IInspectable.
struct IGattCharAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetDescriptors(GUID uuid, void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CharacteristicProperties(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ProtectionLevel(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ProtectionLevel(INT32 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UserDescription(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Uuid(GUID* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AttributeHandle(UINT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PresentationFormats(void** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadValueAsync(void** async) = 0;
};

// IGattReadResult: get_Status / get_Value.
struct IGattReadResultAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Value(void** buffer) = 0;
};

// IBuffer.
struct IBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Length(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Length(UINT32 v) = 0;
};

struct IBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** v) = 0;
};

}  // namespace gatt_abi

namespace {

using namespace gatt_abi;

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Untyped COM object handle: release through IUnknown.
inline void Release(void*& p) {
    if (p) {
        ((IUnknown*)p)->Release();
        p = nullptr;
    }
}

// Kept across polls so the steady-state path only issues one GATT read, instead of re-resolving the
// device and re-enumerating its service/characteristics every 5 seconds. Dropped on any failure;
// the next poll then re-resolves from scratch.
struct GattCache {
    UINT64 address = 0;
    IInspectable* device = nullptr;
    IGattCharAbi* levelChar = nullptr;
};
GattCache g_cache;

void DropCache() {
    Release(g_cache.levelChar);
    Release(g_cache.device);
    g_cache.address = 0;
}

// Blocks until an async operation completes (or times out) and returns the result object as an
// IInspectable, or nullptr on timeout / failure. The caller still owns the async op.
IInspectable* Await(IAsyncOpAbi* op, DWORD timeoutMs) {
    if (!op) return nullptr;
    IAsyncInfo* info = nullptr;
    op->QueryInterface(IID_IAsyncInfo, (void**)&info);
    AsyncStatus st = Started;
    ULONGLONG start = GetTickCount64();
    do {
        if (info) info->get_Status(&st);
        if (st == Completed || st == Canceled || st == Error) break;
        Sleep(50);
    } while (GetTickCount64() - start < timeoutMs);
    IInspectable* res = nullptr;
    if (st == Completed && SUCCEEDED(op->GetResults((void**)&res))) {
        Release(info);
        return res;
    }
    Release(info);
    return nullptr;
}

// Short 16-bit GATT UUID: 0000xxxx-0000-1000-8000-00805F9B34FB.
static GUID ShortUuid(UINT16 u16) {
    GUID g = { 0x00000000, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
    g.Data1 = u16;
    return g;
}

// GattCommunicationStatus: Success = 0.
constexpr INT32 kGattSuccess = 0;

// Reads one already-resolved 2A19 characteristic. Returns 0-100, or -1 on any failure.
int ReadCharPercent(IGattCharAbi* levelChar) {
    IAsyncOpAbi* opRead = nullptr;
    if (FAILED(levelChar->ReadValueAsync((void**)&opRead))) return -1;

    IInspectable* readObj = Await(opRead, 1500);
    Release(opRead);
    if (!readObj) return -1;

    IGattReadResultAbi* readRes = nullptr;
    if (FAILED(readObj->QueryInterface(IID_GattReadResult, (void**)&readRes))) {
        Release(readObj);
        return -1;
    }
    Release(readObj);

    INT32 rst = 0;
    readRes->get_Status(&rst);
    void* bufObj = nullptr;
    if (rst != kGattSuccess || FAILED(readRes->get_Value(&bufObj)) || !bufObj) {
        Release(readRes);
        return -1;
    }
    Release(readRes);

    int percent = -1;
    IBufferAbi* buffer = nullptr;
    if (SUCCEEDED(((IInspectable*)bufObj)->QueryInterface(IID_IBuffer, (void**)&buffer))) {
        UINT32 len = 0;
        buffer->get_Length(&len);
        IBufferByteAccessAbi* bytes = nullptr;
        BYTE* p = nullptr;
        if (len >= 1 && SUCCEEDED(buffer->QueryInterface(IID_BufferByteAccess, (void**)&bytes)) && bytes &&
            SUCCEEDED(bytes->Buffer(&p)) && p)
            percent = p[0];
        Release(bytes);
        Release(buffer);
    }
    Release(bufObj);

    return (percent >= 0 && percent <= 100) ? percent : -1;
}

}  // namespace

// Reads the standard GATT Battery Service level (0x180F / 0x2A19) for the given LE Bluetooth
// address. Returns 0-100, or -1 if the device could not be reached or reported no battery level.
int ReadPhoneBatteryGatt(UINT64 address) {
    if (!address) return -1;
    RoInitialize(RO_INIT_MULTITHREADED);  // our poll thread; harmless if already initialised

    // Fast path: reuse the cached device + 2A19 characteristic from the previous poll so a steady
    // poll only issues one GATT read instead of re-resolving and re-enumerating every round.
    if (g_cache.address == address && g_cache.levelChar) {
        int cached = ReadCharPercent(g_cache.levelChar);
        if (cached >= 0) return cached;
        DropCache();  // handle went stale (link dropped, device asleep); re-resolve below
    }

    IBLEDeviceStaticsAbi* statics = nullptr;
    HSTRING cls = nullptr;
    const wchar_t* clsName = L"Windows.Devices.Bluetooth.BluetoothLEDevice";
    if (FAILED(WindowsCreateString(clsName, (UINT32)wcslen(clsName), &cls))) return -1;
    HRESULT hr = RoGetActivationFactory(cls, IID_BLEDeviceStatics, (void**)&statics);
    WindowsDeleteString(cls);
    if (FAILED(hr) || !statics) return -1;

    IAsyncOpAbi* opDev = nullptr;
    if (FAILED(statics->FromBluetoothAddressAsync(address, (void**)&opDev))) {
        Release(statics);
        return -1;
    }
    Release(statics);

    IInspectable* devObj = Await(opDev, 2000);
    Release(opDev);
    if (!devObj) return -1;

    IBLEDeviceAbi* device = nullptr;
    if (FAILED(devObj->QueryInterface(IID_BLEDevice, (void**)&device))) {
        Release(devObj);
        return -1;
    }

    void* svcObj = nullptr;
    if (FAILED(device->GetGattService(ShortUuid(0x180F), &svcObj)) || !svcObj) {
        Release(device);
        Release(devObj);
        return -1;
    }

    IGattService3Abi* service3 = nullptr;
    if (FAILED(((IInspectable*)svcObj)->QueryInterface(IID_GattDeviceService3, (void**)&service3))) {
        Release(svcObj);
        Release(device);
        Release(devObj);
        return -1;
    }
    Release(svcObj);

    IAsyncOpAbi* opChars = nullptr;
    if (FAILED(service3->GetCharacteristicsAsync((void**)&opChars))) {
        Release(service3);
        Release(device);
        Release(devObj);
        return -1;
    }
    Release(service3);

    IInspectable* charsObj = Await(opChars, 1500);
    Release(opChars);
    if (!charsObj) { Release(device); Release(devObj); return -1; }

    IGattCharsResultAbi* charsRes = nullptr;
    if (FAILED(charsObj->QueryInterface(IID_GattCharacteristicsResult, (void**)&charsRes))) {
        Release(charsObj);
        Release(device);
        Release(devObj);
        return -1;
    }
    Release(charsObj);

    INT32 cst = 0;
    charsRes->get_Status(&cst);
    void* viewObj = nullptr;
    if (cst != kGattSuccess || FAILED(charsRes->get_Characteristics(&viewObj)) || !viewObj) {
        Release(charsRes);
        Release(device);
        Release(devObj);
        return -1;
    }
    Release(charsRes);

    IGattCharViewAbi* view = nullptr;
    if (FAILED(((IInspectable*)viewObj)->QueryInterface(IID_VectorView_GattCharacteristic, (void**)&view))) {
        Release(viewObj);
        Release(device);
        Release(devObj);
        return -1;
    }
    Release(viewObj);

    UINT32 size = 0;
    view->get_Size(&size);
    const GUID battLevel = ShortUuid(0x2A19);
    IGattCharAbi* levelChar = nullptr;
    for (UINT32 i = 0; i < size; i++) {
        void* charObj = nullptr;
        if (FAILED(view->GetAt(i, &charObj)) || !charObj) continue;
        IGattCharAbi* ch = nullptr;
        if (SUCCEEDED(((IInspectable*)charObj)->QueryInterface(IID_GattCharacteristic, (void**)&ch)) && ch) {
            GUID uuid{};
            ch->get_Uuid(&uuid);
            if (uuid == battLevel) {
                levelChar = ch;
                Release(charObj);
                break;
            }
            Release(ch);
        }
        Release(charObj);
    }

    int percent = -1;
    if (levelChar) percent = ReadCharPercent(levelChar);

    if (percent < 0) {
        Release(levelChar);
        Release(view);
        Release(device);
        Release(devObj);
        return -1;
    }

    // Retain the device and characteristic for the next poll; release the intermediate handles.
    DropCache();
    g_cache.address = address;
    g_cache.device = devObj;       // ownership transferred to the cache
    g_cache.levelChar = levelChar;
    Release(view);
    Release(device);
    return percent;
}
