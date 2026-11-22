/*
 * Proton-Scarlett — UWP Bridge System WinRT Internal Contract
 *
 * Clean-room C11 WinRT vtable & GUID definitions derived from Wine/MinGW IDL
 * contracts for UWPHOST.dll winrt_sys subsystem.
 *
 * PE64/Win32 APIs only (zero POSIX/Linux calls).
 */

#ifndef SCARLETT_WINRT_SYS_INTERNAL_H
#define SCARLETT_WINRT_SYS_INTERNAL_H

#include "../include/uwp_bridge_abi.h"

#ifdef _WIN32
#include <windows.h>
#include <inspectable.h>
#include <winstring.h>
#include <eventtoken.h>
#include <asyncinfo.h>
#else
#include "tests/win32_host_shim.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Macros & Memory / String Helpers
 * ============================================================================ */

#define WINRT_SYS_FROM(type, member, ptr) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#ifndef E_BOUNDS
#define E_BOUNDS ((HRESULT)0x8000000BL)
#endif
#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL ((HRESULT)0x8000000EL)
#endif
#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif

static inline int winrt_sys_guid_eq(REFIID a, REFIID b) {
    if (!a || !b) return 0;
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3) return 0;
    for (int i = 0; i < 8; ++i) {
        if (a->Data4[i] != b->Data4[i]) return 0;
    }
    return 1;
}

void *winrt_sys_alloc(size_t bytes);
void  winrt_sys_free(void *ptr);
void *winrt_sys_cotask_alloc(size_t bytes);

HRESULT winrt_sys_make_hstring(const wchar_t *str, HSTRING *out_hstr);
HRESULT winrt_sys_dup_hstring(HSTRING src, HSTRING *out_hstr);
const wchar_t *winrt_sys_hstring_buf(HSTRING hstr, uint32_t *out_len);

HRESULT winrt_sys_get_iids_copy(const GUID *iids, ULONG count, ULONG *out_count, IID **out_iids);
HRESULT winrt_sys_unimplemented(const char *iface_method, void *self);

void winrt_sys_copy_wstr(wchar_t *dst, size_t dst_chars, const wchar_t *src);
void winrt_sys_normalize_path(const wchar_t *in_path, wchar_t *out_path, size_t out_chars);
BOOL winrt_sys_ensure_dir_recursive(const wchar_t *dir_path);

typedef enum winrt_sys_state_kind_e {
    WINRT_SYS_STATE_LOCAL = 0,
    WINRT_SYS_STATE_ROAMING = 1,
    WINRT_SYS_STATE_TEMP = 2,
    WINRT_SYS_STATE_LOCAL_CACHE = 3,
    WINRT_SYS_STATE_SHARED_LOCAL = 4
} winrt_sys_state_kind_t;

void winrt_sys_get_state_dir(winrt_sys_state_kind_t kind, wchar_t *out_path, size_t out_chars);
void winrt_sys_get_install_dir(wchar_t *out_path, size_t out_chars);

/* ============================================================================
 * Standard WinRT Base & System IIDs
 * ============================================================================ */

static const GUID WINRT_IID_IUnknown =
    { 0x00000000u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const GUID WINRT_IID_IInspectable = SCARLETT_IID_IINSPECTABLE_INIT;
static const GUID WINRT_IID_IActivationFactory = SCARLETT_IID_IACTIVATIONFACTORY_INIT;
static const GUID WINRT_IID_IAgileObject = SCARLETT_IID_IAGILEOBJECT_INIT;

/* IClosable: {30d5a829-7fa4-4026-83bb-d75bae4ea99e} */
static const GUID WINRT_IID_IClosable =
    { 0x30d5a829u, 0x7fa4u, 0x4026u, { 0x83u, 0xbbu, 0xd7u, 0x5bu, 0xaeu, 0x4eu, 0xa9u, 0x9eu } };

/* IAsyncInfo: {00000036-0000-0000-c000-000000000046} */
static const GUID WINRT_IID_IAsyncInfo =
    { 0x00000036u, 0x0000u, 0x0000u, { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };

/* IAsyncAction: {5a648006-843a-4da9-865b-9d26e5dfad7b} */
static const GUID WINRT_IID_IAsyncAction =
    { 0x5a648006u, 0x843au, 0x4da9u, { 0x86u, 0x5bu, 0x9du, 0x26u, 0xe5u, 0xdfu, 0xadu, 0x7bu } };

/* IPropertyValue: {4bd682dd-7554-40e9-9a9b-82654ede7e62} */
static const GUID WINRT_IID_IPropertyValue =
    { 0x4bd682ddu, 0x7554u, 0x40e9u, { 0x9au, 0x9bu, 0x82u, 0x65u, 0x4eu, 0xdeu, 0x7eu, 0x62u } };

/* IPropertyValueStatics: {629bdbc8-d932-4ff4-96b9-8d96c5c1e858} */
static const GUID WINRT_IID_IPropertyValueStatics =
    { 0x629bdbc8u, 0xd932u, 0x4ff4u, { 0x96u, 0xb9u, 0x8du, 0x96u, 0xc5u, 0xc1u, 0xe8u, 0x58u } };

/* IPropertySet: {8a43ed9f-f4e6-4421-acf9-1dab2986820c} */
static const GUID WINRT_IID_IPropertySet =
    { 0x8a43ed9fu, 0xf4e6u, 0x4421u, { 0xacu, 0xf9u, 0x1du, 0xabu, 0x29u, 0x86u, 0x82u, 0x0cu } };

/* IMap<HSTRING, IInspectable*>: {1b0d3570-0877-5ec2-8a2c-3b9539506aca} */
static const GUID WINRT_IID_IMap_HSTRING_IInspectable =
    { 0x1b0d3570u, 0x0877u, 0x5ec2u, { 0x8au, 0x2cu, 0x3bu, 0x95u, 0x39u, 0x50u, 0x6au, 0xcau } };

/* IMapView<HSTRING, IInspectable*>: {bb78502a-f79d-54fa-92c9-90c5039fdf7e} */
static const GUID WINRT_IID_IMapView_HSTRING_IInspectable =
    { 0xbb78502au, 0xf79du, 0x54fau, { 0x92u, 0xc9u, 0x90u, 0xc5u, 0x03u, 0x9fu, 0xdfu, 0x7eu } };

/* IObservableMap<HSTRING, IInspectable*>: {236aac9d-fb12-5c4d-a41c-9e445fb4d7ec} */
static const GUID WINRT_IID_IObservableMap_HSTRING_IInspectable =
    { 0x236aac9du, 0xfb12u, 0x5c4du, { 0xa4u, 0x1cu, 0x9eu, 0x44u, 0x5fu, 0xb4u, 0xd7u, 0xecu } };

/* IIterable<IKeyValuePair<HSTRING, IInspectable*>*>: {fe2f3d47-5d47-5499-8374-430c7cda0204} */
static const GUID WINRT_IID_IIterable_KVP_HSTRING_IInspectable =
    { 0xfe2f3d47u, 0x5d47u, 0x5499u, { 0x83u, 0x74u, 0x43u, 0x0cu, 0x7cu, 0xdau, 0x02u, 0x04u } };

/* IIterator<IKeyValuePair<HSTRING, IInspectable*>*>: {5db5fa32-707c-5849-a06b-91c8eb9d10e8} */
static const GUID WINRT_IID_IIterator_KVP_HSTRING_IInspectable =
    { 0x5db5fa32u, 0x707cu, 0x5849u, { 0xa0u, 0x6bu, 0x91u, 0xc8u, 0xebu, 0x9du, 0x10u, 0xe8u } };

/* IKeyValuePair<HSTRING, IInspectable*>: {09335560-6c6b-5a26-9348-97b781132b20} */
static const GUID WINRT_IID_IKeyValuePair_HSTRING_IInspectable =
    { 0x09335560u, 0x6c6bu, 0x5a26u, { 0x93u, 0x48u, 0x97u, 0xb7u, 0x81u, 0x13u, 0x2bu, 0x20u } };

/* IMapChangedEventArgs<HSTRING>: {60141efb-f2f9-5377-96fd-f8c60d9558b5} */
static const GUID WINRT_IID_IMapChangedEventArgs_HSTRING =
    { 0x60141efbu, 0xf2f9u, 0x5377u, { 0x96u, 0xfdu, 0xf8u, 0xc6u, 0x0du, 0x95u, 0x58u, 0xb5u } };

/* Windows.Storage.ApplicationData & StorageFolder */
static const GUID WINRT_IID_IApplicationDataStatics =
    { 0x5612147bu, 0xe843u, 0x45e3u, { 0x94u, 0xd8u, 0x06u, 0x16u, 0x9eu, 0x3cu, 0x8eu, 0x17u } };
static const GUID WINRT_IID_IApplicationDataStatics2 =
    { 0xcd606211u, 0xcf49u, 0x40a4u, { 0xa4u, 0x7cu, 0xc7u, 0xf0u, 0xdbu, 0xbau, 0x81u, 0x07u } };
static const GUID WINRT_IID_IApplicationData =
    { 0xc3da6fb7u, 0xb744u, 0x4b45u, { 0xb0u, 0xb8u, 0x22u, 0x3au, 0x09u, 0x38u, 0xd0u, 0xdcu } };
static const GUID WINRT_IID_IApplicationData2 =
    { 0x9e65cd69u, 0x0ba3u, 0x4e32u, { 0xbeu, 0x29u, 0xb0u, 0x2du, 0xe6u, 0x60u, 0x76u, 0x38u } };
static const GUID WINRT_IID_IApplicationData3 =
    { 0xdc222cf4u, 0x2772u, 0x4c1du, { 0xaau, 0x2cu, 0xc9u, 0xf7u, 0x43u, 0xadu, 0xe8u, 0xd1u } };
static const GUID WINRT_IID_IApplicationDataContainer =
    { 0xc5aefd1eu, 0xf467u, 0x40bau, { 0x85u, 0x66u, 0xabu, 0x64u, 0x0au, 0x44u, 0x1eu, 0x1du } };

static const GUID WINRT_IID_IMapView_HSTRING_AppDataContainer =
    { 0x13624f8du, 0x85ccu, 0x5780u, { 0xa7u, 0x8du, 0x64u, 0xdbu, 0xa5u, 0x8fu, 0x2cu, 0x3cu } };
static const GUID WINRT_IID_IIterable_KVP_HSTRING_AppDataContainer =
    { 0xa785be1du, 0x159eu, 0x53adu, { 0x95u, 0x53u, 0x59u, 0x8bu, 0x03u, 0xdcu, 0xa0u, 0x48u } };
static const GUID WINRT_IID_IIterator_KVP_HSTRING_AppDataContainer =
    { 0xaf3c131du, 0x67aau, 0x5c8du, { 0xaeu, 0x0eu, 0x27u, 0x2bu, 0xa2u, 0x4au, 0xe7u, 0x4fu } };
static const GUID WINRT_IID_IKeyValuePair_HSTRING_AppDataContainer =
    { 0x5adbc543u, 0x2170u, 0x5ad9u, { 0xb3u, 0x5eu, 0x96u, 0x8cu, 0xdbu, 0x78u, 0xfbu, 0x30u } };

static const GUID WINRT_IID_IStorageFolderStatics =
    { 0x08f327ffu, 0x85d5u, 0x48b9u, { 0xaeu, 0xe9u, 0x28u, 0x51u, 0x1eu, 0x33u, 0x9fu, 0x9fu } };
static const GUID WINRT_IID_IStorageFolder =
    { 0x72d1cb78u, 0xb3efu, 0x4f75u, { 0xa8u, 0x0bu, 0x6fu, 0xd9u, 0xdau, 0xe2u, 0x94u, 0x4bu } };
static const GUID WINRT_IID_IStorageFile =
    { 0xfa3f6186u, 0x4214u, 0x428cu, { 0xa6u, 0x4cu, 0x14u, 0xc9u, 0xacu, 0x73u, 0x15u, 0xeau } };
static const GUID WINRT_IID_IStorageItem =
    { 0x4207a996u, 0xca2fu, 0x42f7u, { 0xbdu, 0xe8u, 0x8bu, 0x10u, 0x45u, 0x7au, 0x7fu, 0x30u } };

static const GUID WINRT_IID_IAsyncOperation_StorageFolder =
    { 0x6be9e7d7u, 0xe83au, 0x5cbcu, { 0x80u, 0x2cu, 0x17u, 0x68u, 0x96u, 0x0bu, 0x52u, 0xc3u } };
static const GUID WINRT_IID_IAsyncOperation_StorageFile =
    { 0x5e52f8ceu, 0xacedu, 0x5a42u, { 0x95u, 0xb4u, 0xf6u, 0x74u, 0xddu, 0x84u, 0x88u, 0x5eu } };
static const GUID WINRT_IID_IAsyncOperation_IStorageItem =
    { 0x5fc9c137u, 0xebb7u, 0x5e6cu, { 0x9cu, 0xbau, 0x68u, 0x6fu, 0x2eu, 0xc2u, 0xb0u, 0xbbu } };
static const GUID WINRT_IID_IAsyncOperation_ApplicationData =
    { 0x31456b58u, 0xa5cbu, 0x5c5bu, { 0xbdu, 0x6eu, 0xccu, 0xceu, 0x3au, 0x7bu, 0xf4u, 0xb4u } };
static const GUID WINRT_IID_IVectorView_StorageFolder =
    { 0xe20debc6u, 0xdc4eu, 0x542eu, { 0xa2u, 0xe7u, 0xa2u, 0x4du, 0x19u, 0xc8u, 0xddu, 0x62u } };
static const GUID WINRT_IID_IVectorView_StorageFile =
    { 0x80646519u, 0x5e2au, 0x595du, { 0xa8u, 0xcdu, 0x2au, 0x24u, 0xb4u, 0x06u, 0x7fu, 0x1bu } };
static const GUID WINRT_IID_IVectorView_IStorageItem =
    { 0x85575a41u, 0x06cbu, 0x58d0u, { 0xb9u, 0x8au, 0x7cu, 0x8fu, 0x06u, 0xe6u, 0xe9u, 0xd7u } };

/* Windows.ApplicationModel.Package */
static const GUID WINRT_IID_IPackageStatics =
    { 0x4e534bdfu, 0x2960u, 0x4878u, { 0x97u, 0xa4u, 0x96u, 0x24u, 0xdeu, 0xb7u, 0x2fu, 0x2du } };
static const GUID WINRT_IID_IPackage =
    { 0x163c792fu, 0xbd75u, 0x413cu, { 0xbfu, 0x23u, 0xb1u, 0xfeu, 0x7bu, 0x95u, 0xd8u, 0x25u } };
static const GUID WINRT_IID_IPackage2 =
    { 0xa6612fb6u, 0x7688u, 0x4aceu, { 0x95u, 0xfbu, 0x35u, 0x95u, 0x38u, 0xe7u, 0xaau, 0x01u } };
static const GUID WINRT_IID_IPackage3 =
    { 0x5f738b61u, 0xf86au, 0x4917u, { 0x93u, 0xd1u, 0xf1u, 0xeeu, 0x9du, 0x3bu, 0x35u, 0xd9u } };
static const GUID WINRT_IID_IPackageId =
    { 0x1adb665eu, 0x37c7u, 0x4790u, { 0x99u, 0x80u, 0xddu, 0x7au, 0xe7u, 0x4eu, 0x8bu, 0xb2u } };
static const GUID WINRT_IID_IPackageIdWithMetadata =
    { 0x40577a7cu, 0x0c9eu, 0x443du, { 0x90u, 0x74u, 0x85u, 0x5fu, 0x5cu, 0xe0u, 0xa0u, 0x8du } };
static const GUID WINRT_IID_IPackageStatus =
    { 0x5fe74f71u, 0xa365u, 0x4c09u, { 0xa0u, 0x2du, 0x04u, 0x6du, 0x52u, 0x5eu, 0xa1u, 0xdau } };
static const GUID WINRT_IID_IVectorView_Package =
    { 0x0263c4d4u, 0x195cu, 0x5dc5u, { 0xa7u, 0xcau, 0x68u, 0x06u, 0xceu, 0xcau, 0x42u, 0x0bu } };
static const GUID WINRT_IID_IIterable_Package =
    { 0x69ad6aa7u, 0x0c49u, 0x5f27u, { 0xa5u, 0xebu, 0xefu, 0x4du, 0x59u, 0x46u, 0x7bu, 0x6du } };
static const GUID WINRT_IID_IIterator_Package =
    { 0x0217f069u, 0x025cu, 0x5ee6u, { 0xa8u, 0x7fu, 0xe7u, 0x82u, 0xe3u, 0xb6u, 0x23u, 0xaeu } };

/* Windows.Gaming.Input.Gamepad */
static const GUID WINRT_IID_IGamepadStatics =
    { 0x8bbce529u, 0xd49cu, 0x39e9u, { 0x95u, 0x60u, 0xe4u, 0x7du, 0xdeu, 0x96u, 0xb7u, 0xc8u } };
static const GUID WINRT_IID_IGamepadStatics2 =
    { 0x42676dc5u, 0x0856u, 0x47c4u, { 0x92u, 0x13u, 0xb3u, 0x95u, 0x50u, 0x4cu, 0x3au, 0x3cu } };
static const GUID WINRT_IID_IGamepad =
    { 0xbc7bb43cu, 0x0a69u, 0x3903u, { 0x9eu, 0x9du, 0xa5u, 0x0fu, 0x86u, 0xa4u, 0x5du, 0xe5u } };
static const GUID WINRT_IID_IGamepad2 =
    { 0x3c1689bdu, 0x5915u, 0x4245u, { 0xb0u, 0xc0u, 0xc8u, 0x9fu, 0xaeu, 0x03u, 0x08u, 0xffu } };
static const GUID WINRT_IID_IGameController =
    { 0x1baf6522u, 0x5f64u, 0x42c5u, { 0x82u, 0x67u, 0xb9u, 0xfeu, 0x22u, 0x15u, 0xbfu, 0xbdu } };
static const GUID WINRT_IID_IVectorView_Gamepad =
    { 0xeb97bb69u, 0x09c9u, 0x5a99u, { 0x86u, 0xb2u, 0x3eu, 0x36u, 0x08u, 0x52u, 0x84u, 0xd4u } };
static const GUID WINRT_IID_IIterable_Gamepad =
    { 0x47132ba0u, 0x6b17u, 0x5cd2u, { 0xa8u, 0xbdu, 0xb5u, 0xd3u, 0x44u, 0x3cu, 0xcbu, 0x13u } };
static const GUID WINRT_IID_IIterator_Gamepad =
    { 0x246737e8u, 0x12bcu, 0x5c64u, { 0xafu, 0x52u, 0x06u, 0xdbu, 0x4bu, 0x13u, 0xfau, 0x2fu } };

/* Windows.System.Profile.AnalyticsInfo */
static const GUID WINRT_IID_IAnalyticsInfoStatics =
    { 0x1d5ee066u, 0x188du, 0x5ba9u, { 0x43u, 0x87u, 0xacu, 0xaeu, 0xb0u, 0xe7u, 0xe3u, 0x05u } };
static const GUID WINRT_IID_IAnalyticsVersionInfo =
    { 0x926130b8u, 0x9955u, 0x4c74u, { 0xbdu, 0xc1u, 0x7cu, 0xd0u, 0xdeu, 0xcfu, 0x9bu, 0x03u } };

/* Windows.Gaming.UI.GameBar */
static const GUID WINRT_IID_IGameBarStatics =
    { 0x1db9a292u, 0xcc78u, 0x4173u, { 0xbeu, 0x45u, 0xb6u, 0x1eu, 0x67u, 0x28u, 0x3eu, 0xa7u } };

/* ============================================================================
 * Shared WinRT ABI Vtables & Structs
 * ============================================================================ */

typedef struct WinrtActivationFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *ActivateInstance)(void *This, IInspectable **instance);
} WinrtActivationFactoryVtbl;

typedef struct WinrtAsyncInfoVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, uint32_t *id);
    HRESULT (STDMETHODCALLTYPE *get_Status)(void *This, AsyncStatus *status);
    HRESULT (STDMETHODCALLTYPE *get_ErrorCode)(void *This, HRESULT *error_code);
    HRESULT (STDMETHODCALLTYPE *Cancel)(void *This);
    HRESULT (STDMETHODCALLTYPE *Close)(void *This);
} WinrtAsyncInfoVtbl;

typedef struct WinrtAsyncActionVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *put_Completed)(void *This, void *handler);
    HRESULT (STDMETHODCALLTYPE *get_Completed)(void *This, void **handler);
    HRESULT (STDMETHODCALLTYPE *GetResults)(void *This);
} WinrtAsyncActionVtbl;

typedef struct WinrtAsyncOperationPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *put_Completed)(void *This, void *handler);
    HRESULT (STDMETHODCALLTYPE *get_Completed)(void *This, void **handler);
    HRESULT (STDMETHODCALLTYPE *GetResults)(void *This, void **results);
} WinrtAsyncOperationPtrVtbl;

typedef struct WinrtAsyncCompletedHandlerPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *asyncInfo, AsyncStatus status);
} WinrtAsyncCompletedHandlerPtrVtbl;

typedef struct WinrtAsyncActionCompletedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *action, AsyncStatus status);
} WinrtAsyncActionCompletedHandlerVtbl;

typedef struct WinrtEventHandlerPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, IInspectable *sender, void *args);
} WinrtEventHandlerPtrVtbl;

typedef struct WinrtPackageVersion {
    uint16_t Major;
    uint16_t Minor;
    uint16_t Build;
    uint16_t Revision;
} WinrtPackageVersion;

typedef struct WinrtGamepadReading {
    uint64_t Timestamp;
    uint32_t Buttons;
    double   LeftTrigger;
    double   RightTrigger;
    double   LeftThumbstickX;
    double   LeftThumbstickY;
    double   RightThumbstickX;
    double   RightThumbstickY;
} WinrtGamepadReading;

typedef struct WinrtGamepadVibration {
    double LeftMotor;
    double RightMotor;
    double LeftTrigger;
    double RightTrigger;
} WinrtGamepadVibration;

typedef enum WinrtPropertyType {
    WinrtPropertyType_Empty = 0,
    WinrtPropertyType_UInt8 = 1,
    WinrtPropertyType_Int16 = 2,
    WinrtPropertyType_UInt16 = 3,
    WinrtPropertyType_Int32 = 4,
    WinrtPropertyType_UInt32 = 5,
    WinrtPropertyType_Int64 = 6,
    WinrtPropertyType_UInt64 = 7,
    WinrtPropertyType_Single = 8,
    WinrtPropertyType_Double = 9,
    WinrtPropertyType_Char16 = 10,
    WinrtPropertyType_Boolean = 11,
    WinrtPropertyType_String = 12,
    WinrtPropertyType_Inspectable = 13,
    WinrtPropertyType_DateTime = 14,
    WinrtPropertyType_TimeSpan = 15,
    WinrtPropertyType_Guid = 16
} WinrtPropertyType;

typedef struct WinrtPropertyValueVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Type)(void *This, WinrtPropertyType *value);
    HRESULT (STDMETHODCALLTYPE *get_IsNumericScalar)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetUInt8)(void *This, uint8_t *value);
    HRESULT (STDMETHODCALLTYPE *GetInt16)(void *This, int16_t *value);
    HRESULT (STDMETHODCALLTYPE *GetUInt16)(void *This, uint16_t *value);
    HRESULT (STDMETHODCALLTYPE *GetInt32)(void *This, int32_t *value);
    HRESULT (STDMETHODCALLTYPE *GetUInt32)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *GetInt64)(void *This, int64_t *value);
    HRESULT (STDMETHODCALLTYPE *GetUInt64)(void *This, uint64_t *value);
    HRESULT (STDMETHODCALLTYPE *GetSingle)(void *This, float *value);
    HRESULT (STDMETHODCALLTYPE *GetDouble)(void *This, double *value);
    HRESULT (STDMETHODCALLTYPE *GetChar16)(void *This, wchar_t *value);
    HRESULT (STDMETHODCALLTYPE *GetBoolean)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetString)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetGuid)(void *This, GUID *value);
    HRESULT (STDMETHODCALLTYPE *GetDateTime)(void *This, int64_t *value);
    HRESULT (STDMETHODCALLTYPE *GetTimeSpan)(void *This, int64_t *value);
    HRESULT (STDMETHODCALLTYPE *GetPoint)(void *This, void *value);
    HRESULT (STDMETHODCALLTYPE *GetSize)(void *This, void *value);
    HRESULT (STDMETHODCALLTYPE *GetRect)(void *This, void *value);
    HRESULT (STDMETHODCALLTYPE *GetUInt8Array)(void *This, uint32_t *sz, uint8_t **val);
    HRESULT (STDMETHODCALLTYPE *GetInt16Array)(void *This, uint32_t *sz, int16_t **val);
    HRESULT (STDMETHODCALLTYPE *GetUInt16Array)(void *This, uint32_t *sz, uint16_t **val);
    HRESULT (STDMETHODCALLTYPE *GetInt32Array)(void *This, uint32_t *sz, int32_t **val);
    HRESULT (STDMETHODCALLTYPE *GetUInt32Array)(void *This, uint32_t *sz, uint32_t **val);
    HRESULT (STDMETHODCALLTYPE *GetInt64Array)(void *This, uint32_t *sz, int64_t **val);
    HRESULT (STDMETHODCALLTYPE *GetUInt64Array)(void *This, uint32_t *sz, uint64_t **val);
    HRESULT (STDMETHODCALLTYPE *GetSingleArray)(void *This, uint32_t *sz, float **val);
    HRESULT (STDMETHODCALLTYPE *GetDoubleArray)(void *This, uint32_t *sz, double **val);
    HRESULT (STDMETHODCALLTYPE *GetChar16Array)(void *This, uint32_t *sz, wchar_t **val);
    HRESULT (STDMETHODCALLTYPE *GetBooleanArray)(void *This, uint32_t *sz, boolean **val);
    HRESULT (STDMETHODCALLTYPE *GetStringArray)(void *This, uint32_t *sz, HSTRING **val);
    HRESULT (STDMETHODCALLTYPE *GetInspectableArray)(void *This, uint32_t *sz, IInspectable ***val);
    HRESULT (STDMETHODCALLTYPE *GetGuidArray)(void *This, uint32_t *sz, GUID **val);
    HRESULT (STDMETHODCALLTYPE *GetDateTimeArray)(void *This, uint32_t *sz, int64_t **val);
    HRESULT (STDMETHODCALLTYPE *GetTimeSpanArray)(void *This, uint32_t *sz, int64_t **val);
    HRESULT (STDMETHODCALLTYPE *GetPointArray)(void *This, uint32_t *sz, void **val);
    HRESULT (STDMETHODCALLTYPE *GetSizeArray)(void *This, uint32_t *sz, void **val);
    HRESULT (STDMETHODCALLTYPE *GetRectArray)(void *This, uint32_t *sz, void **val);
} WinrtPropertyValueVtbl;

/* ============================================================================
 * Cross-Module Constructors & Factory Dispatchers
 * ============================================================================ */

HRESULT winrt_sys_create_completed_async_action(HRESULT err_hr, void **out_action);
HRESULT winrt_sys_create_completed_async_op_ptr(
    const GUID *op_iid,
    const wchar_t *runtime_class_name,
    IUnknown *result_obj,
    HRESULT err_hr,
    void **out_op);

HRESULT winrt_sys_create_storage_folder(const wchar_t *path, void **out_folder);
HRESULT winrt_sys_create_storage_file(const wchar_t *path, void **out_file);
HRESULT winrt_sys_create_property_value_string(const wchar_t *str, void **out_pv);
HRESULT winrt_sys_create_property_value_int32(int32_t val, void **out_pv);
HRESULT winrt_sys_create_property_value_uint32(uint32_t val, void **out_pv);
HRESULT winrt_sys_create_property_value_int64(int64_t val, void **out_pv);
HRESULT winrt_sys_create_property_value_uint64(uint64_t val, void **out_pv);
HRESULT winrt_sys_create_property_value_single(float val, void **out_pv);
HRESULT winrt_sys_create_property_value_double(double val, void **out_pv);
HRESULT winrt_sys_create_property_value_boolean(boolean val, void **out_pv);
HRESULT winrt_sys_create_property_value_guid(const GUID *val, void **out_pv);

HRESULT winrt_sys_get_application_data_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_storage_folder_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_package_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_gamepad_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_analytics_info_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_game_bar_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_property_value_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_folder_picker_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_storage_app_perms_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_cryptographic_buffer_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_popup_menu_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_uicommand_factory(REFIID iid, void **out_factory);
HRESULT winrt_sys_get_thread_pool_factory(REFIID iid, void **out_factory);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_WINRT_SYS_INTERNAL_H */
