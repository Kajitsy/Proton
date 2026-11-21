/*
 * Proton-Scarlett — Clean-Room Public API for GDK Runtime
 *   - xgameruntime.dll (Core, XDL 32-slot Dispatch, XTaskQueue, XAsync, XSystem,
 *     XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI, XGameSave,
 *     XGameSaveFiles, XThread, XSAPI, PlayFab)
 *   - libHttpClient.dll (HTTP & WebSocket client)
 *   - XCurl.dll (libcurl-compatible Easy/Multi API)
 *
 * Strictly freestanding C11 / C++20 header (zero libc / POSIX / <windows.h>).
 */

#ifndef SCARLETT_XGAMERUNTIME_API_H
#define SCARLETT_XGAMERUNTIME_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgameruntime_unix_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Handles & Basic Constants
 * ============================================================================ */

typedef uint64_t XTaskQueueHandle;
typedef uint64_t XTaskQueuePortHandle;
typedef uint64_t XTaskQueueRegistrationToken;
typedef uint64_t XUserHandle;
typedef uint64_t XUserLocalId;
typedef uint64_t XPackageMountHandle;
typedef uint64_t XStoreContextHandle;
typedef uint64_t XStoreLicenseHandle;
typedef uint64_t XStoreProductQueryHandle;
typedef uint64_t XGameSaveProviderHandle;
typedef uint64_t XGameSaveContainerHandle;
typedef uint64_t XGameSaveUpdateHandle;
typedef uint64_t HCCallHandle;
typedef uint64_t HCWebsocketHandle;
typedef uint64_t XblContextHandle;
typedef uint64_t XblAchievementsResultHandle;
typedef uint64_t PFServiceConfigHandle;
typedef uint64_t PFEntityHandle;

#ifndef SCARLETT_INFINITE_MS
#define SCARLETT_INFINITE_MS UINT32_C(0xFFFFFFFF)
#endif

/* ============================================================================
 * 1. Core Initialization, 22 Domains & Win64 XDL Dispatch
 * ============================================================================ */

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI InitializeApiImpl(
    uint64_t api_id,
    const scarlett_guid_t *riid,
    void **out_impl);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI QueryApiImpl(
    uint64_t api_id,
    const scarlett_guid_t *riid,
    void **out_impl);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeInitialize(void);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameRuntimeUninitialize(void);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeIsFeatureAvailable(
    scarlett_game_runtime_feature_t feature,
    uint32_t *isAvailable);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeGetDiagnostics(
    void *outBuf,
    uint32_t bufSize);

/* ============================================================================
 * 2. XTaskQueue & XAsync / XAsyncProvider
 * ============================================================================ */

typedef enum XTaskQueueDispatchMode {
    XTaskQueueDispatchMode_ThreadPool           = 0,
    XTaskQueueDispatchMode_SerializedThreadPool = 1,
    XTaskQueueDispatchMode_Manual               = 2,
    XTaskQueueDispatchMode_Immediate            = 3,
    XTaskQueueDispatchMode_Inline               = 4
} XTaskQueueDispatchMode;

typedef enum XTaskQueuePort {
    XTaskQueuePort_Work       = 0,
    XTaskQueuePort_Completion = 1
} XTaskQueuePort;

typedef void (SCARLETT_XGR_CALL *XTaskQueueCallback)(void *context, bool canceled);
typedef void (SCARLETT_XGR_CALL *XTaskQueueMonitorCallback)(
    void *context,
    XTaskQueueHandle queue,
    XTaskQueuePort port);
typedef void (SCARLETT_XGR_CALL *XTaskQueueTerminatedCallback)(void *context);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueCreate(
    XTaskQueueDispatchMode workDispatchMode,
    XTaskQueueDispatchMode completionDispatchMode,
    XTaskQueueHandle *queue);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueCreateComposite(
    XTaskQueuePortHandle workPort,
    XTaskQueuePortHandle completionPort,
    XTaskQueueHandle *queue);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueGetPort(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    XTaskQueuePortHandle *portHandle);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueDuplicateHandle(
    XTaskQueueHandle queueHandle,
    XTaskQueueHandle *duplicatedHandle);

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueCloseHandle(
    XTaskQueueHandle queue);

SCARLETT_XGR_API bool SCARLETT_XGR_CALL XTaskQueueGetCurrentProcessTaskQueue(
    XTaskQueueHandle *queue);

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueSetCurrentProcessTaskQueue(
    XTaskQueueHandle queue);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueSubmitCallback(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    void *callbackContext,
    XTaskQueueCallback callback);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueSubmitDelayedCallback(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    uint32_t delayMs,
    void *callbackContext,
    XTaskQueueCallback callback);

SCARLETT_XGR_API bool SCARLETT_XGR_CALL XTaskQueueDispatch(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    uint32_t timeoutInMs);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueTerminate(
    XTaskQueueHandle queue,
    bool wait,
    void *callbackContext,
    XTaskQueueTerminatedCallback callback);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueRegisterMonitor(
    XTaskQueueHandle queue,
    void *callbackContext,
    XTaskQueueMonitorCallback callback,
    XTaskQueueRegistrationToken *token);

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueUnregisterMonitor(
    XTaskQueueHandle queue,
    XTaskQueueRegistrationToken token);

/* XAsync & XAsyncProvider */

typedef struct XAsyncBlock XAsyncBlock;
typedef void (SCARLETT_XGR_CALL *XAsyncCompletionRoutine)(XAsyncBlock *async);
typedef scarlett_hresult_t (SCARLETT_XGR_CALL *XAsyncWork)(XAsyncBlock *async);

struct XAsyncBlock {
    XTaskQueueHandle queue;
    void *context;
    XAsyncCompletionRoutine callback;
    uint8_t internal[32];
};

typedef enum XAsyncOp {
    XAsyncOp_Begin     = 0,
    XAsyncOp_DoWork    = 1,
    XAsyncOp_GetResult = 2,
    XAsyncOp_Cancel    = 3,
    XAsyncOp_Cleanup   = 4
} XAsyncOp;

typedef struct XAsyncProviderData {
    XAsyncBlock *async;
    uint64_t bufferSize;
    void *buffer;
    void *context;
} XAsyncProviderData;

typedef scarlett_hresult_t (SCARLETT_XGR_CALL *XAsyncProvider)(
    XAsyncOp op,
    const XAsyncProviderData *data);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncBegin(
    XAsyncBlock *asyncBlock,
    void *context,
    const void *identity,
    const char *identityName,
    XAsyncProvider provider);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncSchedule(
    XAsyncBlock *asyncBlock,
    uint32_t delayInMs);

SCARLETT_XGR_API void SCARLETT_XGR_CALL XAsyncComplete(
    XAsyncBlock *asyncBlock,
    scarlett_hresult_t result,
    uint64_t requiredBufferSize);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetStatus(
    XAsyncBlock *asyncBlock,
    bool wait);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetResultSize(
    XAsyncBlock *asyncBlock,
    uint64_t *bufferSize);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetResult(
    XAsyncBlock *asyncBlock,
    const void *identity,
    uint64_t bufferSize,
    void *buffer,
    uint64_t *bufferUsed);

SCARLETT_XGR_API void SCARLETT_XGR_CALL XAsyncCancel(
    XAsyncBlock *asyncBlock);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncRun(
    XAsyncBlock *asyncBlock,
    XAsyncWork work);

/* ============================================================================
 * 3. XSystem & XThread
 * ============================================================================ */

typedef enum XSystemDeviceType {
    XSystemDeviceType_Unknown              = 0,
    XSystemDeviceType_Pc                   = 1,
    XSystemDeviceType_XboxOne              = 2,
    XSystemDeviceType_XboxOneS             = 3,
    XSystemDeviceType_XboxOneX             = 4,
    XSystemDeviceType_XboxOneXDevkit       = 5,
    XSystemDeviceType_XboxScarlettLockhart = 6,
    XSystemDeviceType_XboxScarlettAnaconda = 7,
    XSystemDeviceType_XboxScarlettDevkit   = 8
} XSystemDeviceType;

typedef struct XVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t build;
    uint16_t revision;
} XVersion;

typedef struct XSystemAnalyticsInfo {
    XVersion osVersion;
    XVersion hostingOsVersion;
    char family[64];
    char form[64];
} XSystemAnalyticsInfo;

SCARLETT_XGR_API XSystemDeviceType SCARLETT_XGR_CALL XSystemGetDeviceType(void);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetConsoleId(
    uint32_t consoleIdSize,
    char *consoleId,
    uint32_t *consoleIdUsed);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetXboxLiveSandboxId(
    uint32_t sandboxIdSize,
    char *sandboxId,
    uint32_t *sandboxIdUsed);

SCARLETT_XGR_API XSystemAnalyticsInfo SCARLETT_XGR_CALL XSystemGetAnalyticsInfo(void);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetAppSpecificDeviceId(
    uint32_t appSpecificDeviceIdSize,
    char *appSpecificDeviceId,
    uint32_t *appSpecificDeviceIdUsed);

SCARLETT_XGR_API uint32_t SCARLETT_XGR_CALL XThreadIsTimeSensitive(void);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XThreadSetTimeSensitive(
    uint32_t isTimeSensitive);

/* ============================================================================
 * 4. XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI
 * ============================================================================ */

typedef enum XUserAddOptions {
    XUserAddOptions_None                    = 0x00,
    XUserAddOptions_AddDefaultUserSilently  = 0x01,
    XUserAddOptions_AllowGuests             = 0x02,
    XUserAddOptions_AddDefaultUserAllowingUI= 0x04
} XUserAddOptions;

typedef enum XUserAgeGroup {
    XUserAgeGroup_Unknown = 0,
    XUserAgeGroup_Child   = 1,
    XUserAgeGroup_Teen    = 2,
    XUserAgeGroup_Adult   = 3
} XUserAgeGroup;

typedef enum XUserState {
    XUserState_SignedIn    = 0,
    XUserState_SigningOut  = 1,
    XUserState_SignedOut   = 2
} XUserState;

typedef enum XUserGamertagComponent {
    XUserGamertagComponent_Classic = 0,
    XUserGamertagComponent_Modern  = 1,
    XUserGamertagComponent_UniqueModern = 2
} XUserGamertagComponent;

typedef enum XUserPrivilege {
    XUserPrivilege_CrossPlay            = 185,
    XUserPrivilege_Clubs                = 188,
    XUserPrivilege_Sessions             = 189,
    XUserPrivilege_Broadcast            = 190,
    XUserPrivilege_ManageProfilePrivacy = 196,
    XUserPrivilege_GameDvr              = 198,
    XUserPrivilege_MultiplayerParties   = 203,
    XUserPrivilege_CloudManageSession   = 207,
    XUserPrivilege_CloudJoinSession     = 208,
    XUserPrivilege_CloudSavedGames      = 209,
    XUserPrivilege_SocialNetworkSharing = 220,
    XUserPrivilege_UserGeneratedContent = 247,
    XUserPrivilege_Communications       = 252,
    XUserPrivilege_Multiplayer          = 254,
    XUserPrivilege_AddFriends           = 255
} XUserPrivilege;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserAddAsync(
    XUserAddOptions options,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserAddResult(
    XAsyncBlock *async,
    XUserHandle *newUser);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserDuplicateHandle(
    XUserHandle handle,
    XUserHandle *duplicatedHandle);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XUserCloseHandle(XUserHandle user);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetId(
    XUserHandle user,
    uint64_t *userId);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetLocalId(
    XUserHandle user,
    XUserLocalId *userLocalId);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetGamertag(
    XUserHandle user,
    XUserGamertagComponent gamertagComponent,
    uint32_t gamertagSize,
    char *gamertag,
    uint32_t *gamertagUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetAgeGroup(
    XUserHandle user,
    XUserAgeGroup *ageGroup);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetState(
    XUserHandle user,
    XUserState *state);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserCheckPrivilege(
    XUserHandle user,
    uint32_t options,
    XUserPrivilege privilege,
    bool *hasPrivilege,
    uint32_t *reason);

/* XPackage */
typedef enum XPackageKind {
    XPackageKind_Game = 0,
    XPackageKind_Content = 1
} XPackageKind;

typedef enum XPackageChunkAvailability {
    XPackageChunkAvailability_Ready       = 0,
    XPackageChunkAvailability_Pending     = 1,
    XPackageChunkAvailability_Installable = 2,
    XPackageChunkAvailability_Unavailable = 3
} XPackageChunkAvailability;

typedef struct XPackageDetails {
    char packageIdentifier[128];
    XVersion version;
    XPackageKind kind;
    char displayName[128];
    char description[256];
    char publisher[128];
    char storeId[64];
    bool installing;
    uint32_t index;
    uint32_t count;
} XPackageDetails;

typedef struct XPackageChunkSelector {
    uint32_t type;
    uint32_t chunkId;
    char tag[64];
} XPackageChunkSelector;

typedef bool (SCARLETT_XGR_CALL *XPackageEnumerationCallback)(
    void *context,
    const XPackageDetails *details);
typedef bool (SCARLETT_XGR_CALL *XPackageChunkEnumerationCallback)(
    void *context,
    const XPackageChunkSelector *selector,
    XPackageChunkAvailability availability);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetCurrentProcessPackageIdentifier(
    uint32_t bufferSize,
    char *buffer);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageEnumeratePackages(
    XPackageKind kind,
    uint32_t scope,
    void *context,
    XPackageEnumerationCallback callback);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageEnumerateChunks(
    const char *packageIdentifier,
    uint32_t selectorCount,
    const XPackageChunkSelector *selectors,
    void *context,
    XPackageChunkEnumerationCallback callback);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageMountWithUiAsync(
    const char *packageIdentifier,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageMountWithUiResult(
    XAsyncBlock *async,
    XPackageMountHandle *mount);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XPackageCloseMountHandle(
    XPackageMountHandle mount);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetMountPathSize(
    XPackageMountHandle mount,
    uint32_t *pathSize);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetMountPath(
    XPackageMountHandle mount,
    uint32_t pathSize,
    char *path);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageFindChunkAvailability(
    const char *packageIdentifier,
    uint32_t selectorCount,
    const XPackageChunkSelector *selectors,
    XPackageChunkAvailability *availability);

/* XPersistentLocalStorage */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetPathSize(
    uint32_t *pathSize);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetPath(
    uint32_t pathSize,
    char *path);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetSpaceInfo(
    uint64_t *availableBytes,
    uint64_t *totalBytes);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStoragePromptUserForSpaceAsync(
    uint64_t requestedBytes,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStoragePromptUserForSpaceResult(
    XAsyncBlock *async);

/* XStore */
typedef struct XStoreProduct {
    char storeId[64];
    char title[128];
    char description[256];
    char productKind[32];
    bool isInUserCollection;
    bool hasDigitalDownload;
} XStoreProduct;

typedef bool (SCARLETT_XGR_CALL *XStoreProductQueryCallback)(
    const XStoreProduct *product,
    void *context);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreCreateContext(
    XUserHandle user,
    XStoreContextHandle *storeContextHandle);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseContextHandle(
    XStoreContextHandle storeContextHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForPackageAsync(
    XStoreContextHandle storeContextHandle,
    const char *packageIdentifier,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForPackageResult(
    XAsyncBlock *async,
    XStoreLicenseHandle *storeLicenseHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForDurablesAsync(
    XStoreContextHandle storeContextHandle,
    const char *storeId,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForDurablesResult(
    XAsyncBlock *async,
    XStoreLicenseHandle *storeLicenseHandle);
SCARLETT_XGR_API bool SCARLETT_XGR_CALL XStoreIsLicenseValid(
    XStoreLicenseHandle storeLicenseHandle);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseLicenseHandle(
    XStoreLicenseHandle storeLicenseHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryAssociatedProductsAsync(
    XStoreContextHandle storeContextHandle,
    uint32_t productKinds,
    uint32_t maxItemsToRetrievePerPage,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryAssociatedProductsResult(
    XAsyncBlock *async,
    XStoreProductQueryHandle *productQueryHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryProductForCurrentGameAsync(
    XStoreContextHandle storeContextHandle,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryProductForCurrentGameResult(
    XAsyncBlock *async,
    XStoreProductQueryHandle *productQueryHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreEnumerateProductsQuery(
    XStoreProductQueryHandle productQueryHandle,
    void *context,
    XStoreProductQueryCallback callback);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseProductsQueryHandle(
    XStoreProductQueryHandle productQueryHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryConsumableBalanceRemainingAsync(
    XStoreContextHandle storeContextHandle,
    const char *storeProductId,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryConsumableBalanceRemainingResult(
    XAsyncBlock *async,
    uint32_t *quantity);

/* XGameUI */
typedef enum XGameUiMessageDialogButton {
    XGameUiMessageDialogButton_First  = 0,
    XGameUiMessageDialogButton_Second = 1,
    XGameUiMessageDialogButton_Third  = 2
} XGameUiMessageDialogButton;

typedef enum XGameUiNotificationPositionHint {
    XGameUiNotificationPositionHint_BottomCenter = 0,
    XGameUiNotificationPositionHint_BottomLeft   = 1,
    XGameUiNotificationPositionHint_BottomRight  = 2,
    XGameUiNotificationPositionHint_TopCenter    = 3,
    XGameUiNotificationPositionHint_TopLeft      = 4,
    XGameUiNotificationPositionHint_TopRight     = 5
} XGameUiNotificationPositionHint;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowMessageDialogAsync(
    XAsyncBlock *async,
    const char *titleText,
    const char *contentText,
    const char *firstButtonText,
    const char *secondButtonText,
    const char *thirdButtonText,
    XGameUiMessageDialogButton defaultButton,
    XGameUiMessageDialogButton cancelButton);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowMessageDialogResult(
    XAsyncBlock *async,
    XGameUiMessageDialogButton *resultButton);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryAsync(
    XAsyncBlock *async,
    const char *titleText,
    const char *descriptionText,
    const char *defaultText,
    uint32_t inputScope,
    uint32_t maxTextLength);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryResultSize(
    XAsyncBlock *async,
    uint32_t *resultTextBufferSize);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryResult(
    XAsyncBlock *async,
    uint32_t resultTextBufferSize,
    char *resultTextBuffer,
    uint32_t *resultTextBufferUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowErrorDialogAsync(
    XAsyncBlock *async,
    const char *context,
    scarlett_hresult_t errorCode);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowErrorDialogResult(
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowPlayerProfileCardAsync(
    XAsyncBlock *async,
    XUserHandle requestor,
    uint64_t targetUser);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowPlayerProfileCardResult(
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiSetNotificationPositionHint(
    XGameUiNotificationPositionHint position);

/* ============================================================================
 * 5. XGameSave & XGameSaveFiles (All 15 error codes 0x80830001..0x8083000F)
 * ============================================================================ */

typedef struct XGameSaveContainerInfo {
    char name[256];
    char displayName[256];
    uint32_t blobCount;
    uint64_t totalSize;
    uint64_t lastModifiedTime;
    bool needsSync;
} XGameSaveContainerInfo;

typedef struct XGameSaveBlobInfo {
    char name[128];
    uint32_t size;
} XGameSaveBlobInfo;

typedef struct XGameSaveBlob {
    XGameSaveBlobInfo info;
    uint8_t *data;
} XGameSaveBlob;

typedef bool (SCARLETT_XGR_CALL *XGameSaveContainerInfoCallback)(
    const XGameSaveContainerInfo *container,
    void *context);

typedef bool (SCARLETT_XGR_CALL *XGameSaveBlobInfoCallback)(
    const XGameSaveBlobInfo *info,
    void *context);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProvider(
    XUserHandle requestingUser,
    const char *configurationId,
    bool syncOnDemand,
    XGameSaveProviderHandle *provider);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProviderAsync(
    XUserHandle requestingUser,
    const char *configurationId,
    bool syncOnDemand,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProviderResult(
    XAsyncBlock *async,
    XGameSaveProviderHandle *provider);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseProvider(
    XGameSaveProviderHandle provider);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuota(
    XGameSaveProviderHandle provider,
    int64_t *remainingQuota);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuotaAsync(
    XGameSaveProviderHandle provider,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuotaResult(
    XAsyncBlock *async,
    int64_t *remainingQuota);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainer(
    XGameSaveProviderHandle provider,
    const char *containerName);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainerAsync(
    XGameSaveProviderHandle provider,
    const char *containerName,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainerResult(
    XAsyncBlock *async);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveCreateContainer(
    XGameSaveProviderHandle provider,
    const char *containerName,
    XGameSaveContainerHandle *containerContext);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseContainer(
    XGameSaveContainerHandle context);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateContainers(
    XGameSaveProviderHandle provider,
    void *context,
    XGameSaveContainerInfoCallback callback);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateContainersByName(
    XGameSaveProviderHandle provider,
    const char *containerNamePrefix,
    void *context,
    XGameSaveContainerInfoCallback callback);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateBlobInfo(
    XGameSaveContainerHandle container,
    void *context,
    XGameSaveBlobInfoCallback callback);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateBlobInfoByName(
    XGameSaveContainerHandle container,
    const char *blobNamePrefix,
    void *context,
    XGameSaveBlobInfoCallback callback);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobData(
    XGameSaveContainerHandle container,
    const char **blobNames,
    uint32_t *countOfBlobs,
    uint32_t blobsSize,
    XGameSaveBlob *blobData);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobDataAsync(
    XGameSaveContainerHandle container,
    const char **blobNames,
    uint32_t countOfBlobs,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobDataResult(
    XAsyncBlock *async,
    uint32_t blobsSize,
    XGameSaveBlob *blobData,
    uint32_t *countOfBlobs);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveCreateUpdate(
    XGameSaveContainerHandle container,
    const char *containerDisplayName,
    XGameSaveUpdateHandle *updateContext);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitBlobWrite(
    XGameSaveUpdateHandle updateContext,
    const char *blobName,
    const uint8_t *data,
    uint64_t size);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitBlobDelete(
    XGameSaveUpdateHandle updateContext,
    const char *blobName);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdate(
    XGameSaveUpdateHandle updateContext);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdateAsync(
    XGameSaveUpdateHandle updateContext,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdateResult(
    XAsyncBlock *async);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseUpdate(
    XGameSaveUpdateHandle updateContext);

/* XGameSaveFiles */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetFolderWithUiAsync(
    XUserHandle requestingUser,
    const char *configurationId,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetFolderWithUiResult(
    XAsyncBlock *async,
    uint32_t folderSize,
    char *folder);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetRemainingQuota(
    XUserHandle requestingUser,
    const char *configurationId,
    int64_t *remainingQuota);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveFilesCloseProvider(
    XUserHandle requestingUser,
    const char *configurationId);

/* ============================================================================
 * 6. XSAPI (Xbl*) & PlayFab (PF*) Autonomous Local Implementations
 * ============================================================================ */

typedef struct XblInitArgs {
    XTaskQueueHandle queue;
    const char *scid;
} XblInitArgs;

typedef struct XblAchievement {
    char id[64];
    char name[128];
    uint32_t progressState; /* 0=Unknown, 1=Achieved, 2=NotStarted, 3=InProgress */
    uint32_t percentComplete;
} XblAchievement;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblInitialize(const XblInitArgs *args);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblCleanupAsync(XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextCreateHandle(
    XUserHandle user,
    XblContextHandle *context);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XblContextCloseHandle(XblContextHandle xboxLiveContextHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextDuplicateHandle(
    XblContextHandle xboxLiveContextHandle,
    XblContextHandle *duplicatedHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextGetUser(
    XblContextHandle context,
    XUserHandle *user);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsGetAchievementsForTitleIdAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    uint32_t titleId,
    uint32_t type,
    bool unlockedOnly,
    uint32_t orderBy,
    uint32_t skipItems,
    uint32_t maxItems,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsGetAchievementsForTitleIdResult(
    XAsyncBlock *async,
    XblAchievementsResultHandle *result);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsResultGetAchievements(
    XblAchievementsResultHandle resultHandle,
    const XblAchievement **achievements,
    uint32_t *achievementsCount);
SCARLETT_XGR_API void SCARLETT_XGR_CALL XblAchievementsResultCloseHandle(
    XblAchievementsResultHandle resultHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsUpdateAchievementAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    const char *achievementId,
    uint32_t percentComplete,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblUserStatisticsGetSingleUserStatisticAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    const char *serviceConfigurationId,
    const char *statisticName,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblUserStatisticsGetSingleUserStatisticResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblSocialGetSocialRelationshipsAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    uint32_t filter,
    uint32_t startIndex,
    uint32_t maxItems,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblSocialGetSocialRelationshipsResult(
    XAsyncBlock *async,
    uint32_t *outRelationshipCount);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblMatchmakingCreateMatchTicketAsync(
    XblContextHandle xboxLiveContext,
    const char *hopperName,
    const char *attributesJson,
    uint32_t ticketTimeoutSeconds,
    XAsyncBlock *async);

/* PlayFab */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFInitialize(XTaskQueueHandle backgroundQueue);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFUninitializeAsync(XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFServiceConfigCreateHandle(
    const char *connectionString,
    const char *titleId,
    PFServiceConfigHandle *serviceConfigHandle);
SCARLETT_XGR_API void SCARLETT_XGR_CALL PFServiceConfigCloseHandle(
    PFServiceConfigHandle serviceConfigHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFAuthenticationLoginWithXboxAsync(
    PFServiceConfigHandle serviceConfigHandle,
    XUserHandle user,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFAuthenticationLoginWithXboxResult(
    XAsyncBlock *async,
    PFEntityHandle *entityHandle);
SCARLETT_XGR_API void SCARLETT_XGR_CALL PFEntityCloseHandle(PFEntityHandle entityHandle);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFTitleDataGetTitleDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFTitleDataGetTitleDataResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataGetUserDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataGetUserDataResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataUpdateUserDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    const char *valueJson,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataUpdateUserDataResult(
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFMatchmakingCreateMatchmakingTicketAsync(
    PFEntityHandle entityHandle,
    const char *queueName,
    XAsyncBlock *async);

/* ============================================================================
 * 7. libHttpClient.dll
 * ============================================================================ */

typedef struct HCInitArgs {
    XTaskQueueHandle queue;
} HCInitArgs;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCInitialize(const HCInitArgs *args);
SCARLETT_XGR_API void SCARLETT_XGR_CALL HCCleanup(void);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCGetLibVersion(const char **version);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallCreate(HCCallHandle *call);
SCARLETT_XGR_API HCCallHandle SCARLETT_XGR_CALL HCCallDuplicateHandle(HCCallHandle call);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallCloseHandle(HCCallHandle call);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetUrl(
    HCCallHandle call,
    const char *method,
    const char *url);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetRequestBodyBytes(
    HCCallHandle call,
    const uint8_t *requestBodyBytes,
    uint32_t requestBodySize);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetRequestBodyString(
    HCCallHandle call,
    const char *requestBodyString);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetHeader(
    HCCallHandle call,
    const char *headerName,
    const char *headerValue,
    bool allowTracing);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallRequestSetTimeout(
    HCCallHandle call,
    uint32_t timeoutInSeconds);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallPerformAsync(
    HCCallHandle call,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetStatusCode(
    HCCallHandle call,
    uint32_t *statusCode);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetNetworkErrorCode(
    HCCallHandle call,
    scarlett_hresult_t *networkErrorCode,
    uint32_t *platformNetworkErrorCode);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseString(
    HCCallHandle call,
    const char **responseString);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseBodyBytesSize(
    HCCallHandle call,
    uint64_t *bufferSize);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetResponseBodyBytes(
    HCCallHandle call,
    uint64_t bufferSize,
    uint8_t *buffer,
    uint64_t *bufferUsed);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCCallResponseGetHeader(
    HCCallHandle call,
    const char *headerName,
    const char **headerValue);

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketCreate(
    HCWebsocketHandle *websocket,
    void *messageFunc,
    void *binaryMessageFunc,
    void *closeFunc,
    void *functionContext);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketConnectAsync(
    const char *uri,
    const char *subProtocol,
    HCWebsocketHandle websocket,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketSendMessageAsync(
    HCWebsocketHandle websocket,
    const char *message,
    XAsyncBlock *async);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketDisconnect(
    HCWebsocketHandle websocket);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCWebSocketCloseHandle(
    HCWebsocketHandle websocket);
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL HCGlobalSetHttpCallPerformFunction(
    void *performFunc,
    void *context);

/* ============================================================================
 * 8. XCurl.dll (Clean-Room libcurl-compatible Interface)
 * ============================================================================ */

typedef uint64_t CURL;
typedef uint64_t CURLM;

typedef struct curl_slist {
    char *data;
    struct curl_slist *next;
} curl_slist;

typedef enum CURLcode {
    CURLE_OK               = 0,
    CURLE_UNSUPPORTED_PROTOCOL = 1,
    CURLE_FAILED_INIT      = 2,
    CURLE_URL_MALFORMAT    = 3,
    CURLE_COULDNT_RESOLVE_HOST = 6,
    CURLE_COULDNT_CONNECT  = 7,
    CURLE_OPERATION_TIMEDOUT = 28,
    CURLE_BAD_FUNCTION_ARGUMENT = 43,
    CURLE_SSL_CACERT_BADFILE = 77
} CURLcode;

typedef enum CURLMcode {
    CURLM_OK               = 0,
    CURLM_BAD_HANDLE       = 1,
    CURLM_BAD_EASY_HANDLE  = 2,
    CURLM_INTERNAL_ERROR   = 4
} CURLMcode;

typedef enum CURLoption {
    CURLOPT_URL            = 10002,
    CURLOPT_PORT           = 3,
    CURLOPT_TIMEOUT        = 13,
    CURLOPT_POST           = 47,
    CURLOPT_POSTFIELDS     = 10015,
    CURLOPT_POSTFIELDSIZE  = 60,
    CURLOPT_CAINFO         = 10065,
    CURLOPT_CAPATH         = 10097,
    CURLOPT_HTTPHEADER     = 10023,
    CURLOPT_WRITEDATA      = 10001,
    CURLOPT_WRITEFUNCTION  = 20011
} CURLoption;

typedef enum CURLINFO {
    CURLINFO_EFFECTIVE_URL   = 0x100001,
    CURLINFO_RESPONSE_CODE   = 0x200002,
    CURLINFO_OS_ERRNO        = 0x200019,
    CURLINFO_CAINFO          = 0x10003D,
    CURLINFO_CAPATH          = 0x10003E
} CURLINFO;

SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_global_init(int64_t flags);
SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_global_cleanup(void);
SCARLETT_XGR_API const char * SCARLETT_XGR_CALL curl_version(void);
SCARLETT_XGR_API CURL * SCARLETT_XGR_CALL curl_easy_init(void);
SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_easy_cleanup(CURL *curl);
SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_setopt(CURL *curl, CURLoption option, ...);
SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_perform(CURL *curl);
SCARLETT_XGR_API CURLcode SCARLETT_XGR_CALL curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
SCARLETT_XGR_API const char * SCARLETT_XGR_CALL curl_easy_strerror(CURLcode code);
SCARLETT_XGR_API struct curl_slist * SCARLETT_XGR_CALL curl_slist_append(
    struct curl_slist *list,
    const char *string);
SCARLETT_XGR_API void SCARLETT_XGR_CALL curl_slist_free_all(struct curl_slist *list);
SCARLETT_XGR_API CURLM * SCARLETT_XGR_CALL curl_multi_init(void);
SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_cleanup(CURLM *multi_handle);
SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_add_handle(CURLM *multi_handle, CURL *curl_handle);
SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_remove_handle(CURLM *multi_handle, CURL *curl_handle);
SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_perform(CURLM *multi_handle, int *running_handles);
SCARLETT_XGR_API CURLMcode SCARLETT_XGR_CALL curl_multi_poll(
    CURLM *multi_handle,
    void *extra_fds,
    uint32_t extra_nfds,
    int timeout_ms,
    int *numfds);
SCARLETT_XGR_API void * SCARLETT_XGR_CALL curl_share_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XGAMERUNTIME_API_H */
