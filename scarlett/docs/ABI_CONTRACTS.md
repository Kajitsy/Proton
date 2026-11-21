# Proton-Scarlett — Архитектурные и ABI-контракты (Phase 0)

Данный документ является нормативной спецификацией (Single Source of Truth) проекта **Proton-Scarlett** — Clean-Room адаптации `ValveSoftware/Proton` (ветка `proton_11.0`) для исполнения приложений и игр семейства **Xbox Series X|S (`Gaming.Xbox.Scarlett.x64`)** на Linux x86_64.

Нормативные заголовочные контракты (C11 / C++20, без платформенных зависимостей):
- `scarlett/include/scarlett_errors.h` — единая таблица кодов ошибок и `HRESULT`
- `scarlett/include/scarlett_abi_common.h` — версии ABI, профили консоли, диапазоны памяти, `DSTORAGE_REQUEST_OPTIONS`, `IBcpackDecoder`, 22 домена `XGameRuntimeFeature`
- `scarlett/include/scarlett_unix_call.h` — единый контракт кодов `WINE_UNIX_CALL` для всех `unixlib`-модулей
- `scarlett/include/scarlett_stub_thunk.h` — контракт диагностического трамплина авто-заглушек
- `scarlett/profiles/scarlett_app_db.toml` — декларативная база профилей классов AAA-движков

---

## 1. Базовая архитектура Proton 11.0 и граница PE / unixlib

### 1.1 Платформа и сборочная среда
- **Базовая ветка**: `ValveSoftware/Proton` `proton_11.0`, версия префикса `CURRENT_PREFIX_VERSION="11.0-100"`.
- **Сборочный контейнер**: `steamrt4` SDK (`registry.gitlab.steamos.cloud/proton/steamrt4/sdk/x86_64:4.0.20260331.220802-0`), тулчейн `LLVM 22.1.8` / `llvm-mingw` / `MinGW 13.0.0` / `Ninja 1.13.1` / `Rust`.
- **Синхронизация ядра**:
  - Основной механизм: **`NTSYNC` (`/dev/ntsync`)**, отключаемый переменной `PROTON_NO_NTSYNC`. Используется для детерминированной низколатентной реализации Win32-примитивов (`Event`, `Semaphore`, `Mutex`) и диспетчеризации `XTaskQueue` / `XAsync`.
  - Фоллбэк: `WINEFSYNC=1` (отключается `PROTON_NO_FSYNC`). Устаревший `PROTON_NO_ESYNC` в Proton 11.0 выведен из эксплуатации.

### 1.2 Жёсткое разделение PE (`x86_64-windows`) и `unixlib` (`x86_64-unix`)
Wine 11.0 требует абсолютной изоляции Windows PE64-модулей от нативного Linux ELF64-пространства:

| Слой | Целевой каталог в дистрибутиве | ABI | Разрешённые зависимости | Запрещённые зависимости |
| :--- | :--- | :--- | :--- | :--- |
| **PE64 DLL** | `$(DST_LIBDIR)/wine/scarlett/x86_64-windows` | Microsoft x64 (`sizeof(long) == 4`) | Win32/NT API (`ntdll`, `kernelbase`), `wine/unixlib.h`, заголовки `scarlett/include/*.h` | **0 прямых ссылок** на `libc.so.6`, `libvulkan.so.1`, `liburing.so`, `libdrm.so`, POSIX syscalls |
| **Unixlib `.so`** | `$(DST_LIBDIR)/wine/scarlett/x86_64-unix` | System V AMD64 (`sizeof(long) == 8`) | Linux syscalls (`io_uring`, `userfaultfd`, `memfd_create`), `libvulkan`, `zlib-ng`/`libdeflate`, PipeWire/FFmpeg/evdev, `scarlett/include/*.h` | Запрещён прямой вызов Win32 PE-функций без обратного thunk |
| **Маркер версии** | `$(DST_LIBDIR)/wine/scarlett/version` | UTF-8 текст | Хранит версию/хэш сборки подсистемы Scarlett | — |

Правила пересечения границы `PE <-> unixlib`:
1. Каждая PE64 DLL, взаимодействующая с нативным Linux-бэкендом, вызывает `__wine_init_unix_call()` ровно один раз в `DllMain(DLL_PROCESS_ATTACH)`.
2. Все обращения к `xmem_unix.so`, `dstorage_xs_unix.so`, `audio_scarlett_unix.so` и `loader_unix.so` выполняются исключительно через `WINE_UNIX_CALL(opcode, &params)` (обёртка `SCARLETT_UNIX_CALL` в `scarlett/include/scarlett_unix_call.h`).
3. Код `opcode == 0` (`*_UNIX_INIT`) в каждом `unixlib`-модуле зарезервирован под рукопожатие версии (`SCARLETT_UNIX_CALL_ABI_VERSION == 1`) и идентификатора модуля (`scarlett_unix_module_id_t`). При несовпадении возвращается `SCARLETT_ERR_ABI_VERSION_MISMATCH`.
4. Все структуры параметров через `WINE_UNIX_CALL` используют строго фиксированные типы (`uint32_t`, `int32_t`, `uint64_t`, `float`), явное выравнивание и проверки `SCARLETT_STATIC_ASSERT` по размеру в байтах. Использование `long`, `size_t` или `bool` в структурах межграничного обмена запрещено.

### 1.3 Активация режима Scarlett и изоляция ПК-игр
Обычный запуск ПК-игр в Proton 11.0 **не изменяется**. Режим Scarlett активируется лаунчером `proton` согласно матрице переменных окружения:
- `PROTON_XBOX_SCARLETT=0|1|auto` (по умолчанию `auto`): включается автоматически **только** если рядом с запускаемым PE64-бинарем присутствует `MicrosoftGame.Config`, в котором `TargetDeviceFamily` содержит `Scarlett` или `XboxGen9`. При `0` всегда выключено; при `1` включено принудительно.
- `PROTON_XBOX_MODEL=anaconda|lockhart|devkit` (по умолчанию `anaconda`).
- `PROTON_XBOX_OVERLAY_DIR=<path>` (по умолчанию `pfx/drive_c/scarlett_overlay`).
- `PROTON_XBOX_SHADER_MODE=exact|adaptive|fast_f32` (по умолчанию `adaptive`).
- `PROTON_XBOX_STRICT=0|1` (по умолчанию `0`).
- `PROTON_XBOX_BCPACK_PLUGIN=<path.so>` (по умолчанию не задано).
- `PROTON_XBOX_NET_MODE=offline|lan|passthrough` (по умолчанию `offline`).

При активном режиме Scarlett:
- `lib/wine/scarlett/x86_64-windows` и `lib/wine/scarlett/x86_64-unix` добавляются в `WINEDLLPATH` / `LD_LIBRARY_PATH`.
- В `WINEDLLOVERRIDES` принудительно выставляется приоритет `=b` (`builtin`) для всех 18 консольных DLL во избежание ошибки загрузчика `0xC0000139` (`STATUS_ENTRYPOINT_NOT_FOUND`):
  `xgameruntime`, `xgameplatform`, `d3d12_xs`, `xg_xs`, `xmem`, `kernelx`, `pixevt`, `dstorage_xs`, `XboxBvhBuilder`, `GameInput`, `xaudio2_9`, `AcpHal`, `Bink2Scarlett`, `amd_fidelityfx_loader_xbox`, `amd_fidelityfx_upscaler_xbox`, `libHttpClient`, `XCurl`, `api-ms-win-core-psm-appnotify-l1-1-0`.
- При `PROTON_XBOX_NET_MODE=passthrough|lan` системные CA-сертификаты (`SSL_CERT_FILE`, `SSL_CERT_DIR`) пробрасываются в `pressure-vessel` / окружение процесса.

---

## 2. Жёсткие правила Clean-Room и политика Zero Silent Stubs

### 2.1 Clean-Room и отказ от DRM-обхода
1. В репозитории **категорически запрещено** хранение, загрузка, редистрибуция или обратная интеграция проприетарных бинарей/DLL Microsoft/Xbox Game OS, криптографических ключей (`CIK`, `XVD`/`XVC` keys, лицензий PlayReady) и любых механизмов обхода DRM.
2. Загрузчик пакетов XVD/XVC работает исключительно с расшифрованными / незашифрованными образами. При обнаружении зашифрованного пакета XVC/XVD (см. раздел 3.2) загрузчик **обязан немедленно** завершить операцию монтирования с кодом `SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED` (`0xA05C0001`) без попыток дешифрования.
3. Поддержка проприетарных форматов сжатия (в частности `BCPack`) допускается исключительно через внешний опциональный Clean-Room плагин по контракту `IBcpackDecoder` (`PROTON_XBOX_BCPACK_PLUGIN`).

### 2.2 Запрет тихих заглушек (Diagnostic Auto-Stub Trampoline)
1. Ни один экспорт PE64 DLL и ни один слот vtable не имеет права «молча» возвращать фиктивный успех (`S_OK` / `0`) без фактической реализации контракта.
2. Каждый нереализованный символ, ординал или удалённый слот vtable обязан маршрутизироваться в диагностический трамплин авто-заглушек (`scarlett/include/scarlett_stub_thunk.h`).
3. При срабатывании трамплин (`scarlett_stub_dispatch_win64`) обязан:
   - сохранить регистры аргументов Win64 ABI (`RCX`, `RDX`, `R8`, `R9`) и адрес возврата `[RSP]`;
   - вычислить имя вызывающего модуля (`caller_module_name`), его базу (`caller_module_base`) и смещение `caller_rva = return_address - caller_module_base`;
   - заполнить 472-байтную самодостаточную структуру `scarlett_stub_context_t` (включая `dll_name`, `symbol_name`, `ordinal`, `vtable_slot`, `thread_id`, `strict_mode`, `hit_count`);
   - отправить контекст в диагностический лог (через `SCARLETT_LOADER_UNIX_REPORT_STUB` или тестовый sink).
4. Реакция трамплина определяется `PROTON_XBOX_STRICT`:
   - `PROTON_XBOX_STRICT=0`: `SCARLETT_STUB_ACTION_LOG_AND_FAIL` — запись полного контекста в лог и возврат отрицательного `HRESULT` (проходит через `scarlett_stub_enforce_error()`, что исключает случайный возврат `S_OK`; по умолчанию `SCARLETT_ERR_UNIMPLEMENTED_STUB` или `E_NOTIMPL`).
   - `PROTON_XBOX_STRICT=1`: `SCARLETT_STUB_ACTION_LOG_AND_ABORT` — запись полного контекста в лог и немедленный детерминированный останов процесса с кодом `SCARLETT_ERR_STRICT_STUB_ABORT`.

---

## 3. Контракты 8 подсистем Proton-Scarlett

### 3.1 Подсистема 1: `scarlett-xdl` (Game OS 2020..2026 & API Dispatch)

- **Версионирование ABI (`abi_t`)**:
  - Поддерживается диапазон версий Game OS / GXDK **`2020..2026`**.
  - Структура `scarlett_abi_version_t` (16 байт, `scarlett/include/scarlett_abi_common.h`): поля `major`, `minor`, `build`, `revision` (по `uint16_t`), тег GXDK `gxdk_magic = 0x585D070E` (`SCARLETT_GXDK_MAGIC`) и `flags` (`uint32_t`).
- **Атрибуты IDL-языка `scarlett-xdl`**:
  - `[uuid("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX")]` — явный GUID COM/API-интерфейса.
  - `[no_uuid]` — плоская таблица/интерфейс без GUID `QueryInterface`.
  - `[force_abi(major, minor, build, revision)]` — принудительная фиксация ABI-среза для символа или таблицы.
  - `[added(major, minor, build, revision)]` — минимальная версия Game OS, в которой появляется метод/слот.
  - `[removed(major, minor, build, revision)]` — версия, начиная с которой слот помечается удалённым (сохраняется позиция в vtable, но слот заполняется auto-stub thunk с флагом `SCARLETT_STUB_FLAG_REMOVED_IN_ABI`).
  - `[conditional("expr")]` — условная генерация по модели консоли (`anaconda`/`lockhart`/`devkit`) или профилю возможностей.
  - `[ordinal(N)]` — привязка экспорта к точному PE64-ординалу в генерируемом `.def`.
  - `[unimplemented]` — обязательная генерация записи `scarlett_stub_descriptor_t` и трамплина вместо тихой заглушки.
- **Win64 Register ABI для `QueryApiImpl`**:
  - Соглашение вызова: Microsoft x64 calling convention (32-байтный shadow space, 16-байтное выравнивание стека, сохранение `RBX, RBP, RDI, RSI, R12..R15, XMM6..XMM15`).
  - Регистры входа:
    - **`RCX`** = `api_id` (идентификатор/указатель на дескриптор API)
    - **`RDX`** = `riid` (`const scarlett_guid_t *`, запрашиваемый интерфейс)
    - **`R8`**  = `out_impl` (`void **`, выходной указатель на реализацию/vtable)
  - Возврат в **`RAX`**: `SCARLETT_OK` (`0`), `SCARLETT_E_NOINTERFACE` (`0x80004002`) или `SCARLETT_E_INVALIDARG` (`0x80070057`).
- **Базовые 32-слотовые таблицы виртуальных методов (`SCARLETT_XDL_VTABLE_SLOTS == 32`, 256 байт)**:
  1. `{073B7DCB-1FCF-4030-94BE-E3C9EB623428}` (`SCARLETT_GUID_SYSTEM_DISPATCH_INIT`) — корневая 32-слотовая таблица диспетчеризации Game OS System/Runtime API.
  2. `{37E56907-2F10-41E8-B72F-36EDB185331A}` (`SCARLETT_GUID_PLATFORM_DISPATCH_INIT`) — вторичная 32-слотовая таблица сервисов платформы, памяти и жизненного цикла тайтла.
  - Инвариант: ни один из 32 слотов обеих таблиц не может быть `NULL`; каждый нереализованный или `[removed]` слот указывает на индивидуальный thunk с точными `vtable_slot`, `dll_name` и `symbol_name`.

---

### 3.2 Подсистема 2: Loader, VFS `GAME:` CoW, XVD/XVC (`0x000..0x200`), `xmem.dll` + `kernelx.dll` + `xmem_unix.so`

#### Loader, VFS и XVD/XVC (`loader_unix.so` + `kernelx.dll`)
- **Виртуальная ФС `GAME:` с CoW-оверлеем**:
  - Исходный каталог игры (или смонтированный незашифрованный XVD) всегда открывается строго **read-only**.
  - Все модификации, временные файлы и whiteout-удаления перенаправляются в каталог CoW-оверлея `PROTON_XBOX_OVERLAY_DIR` (по умолчанию `pfx/drive_c/scarlett_overlay`).
  - Попытка прямой записи в read-only базовый слой минуя CoW-оверлей блокируется с `SCARLETT_ERR_VFS_READONLY_VIOLATION`.
- **Парсер заголовка XVD/XVC (`0x000..0x200` + `0x200`)**:
  - Диапазон `0x000..0x1FF` (512 байт, `SCARLETT_XVD_SIGNATURE_SIZE`): преамбула/подпись заголовка (валидируется на размер и границы буфера без обхода DRM).
  - Смещение `0x200` (`SCARLETT_XVD_HEADER_OFFSET`): сигнатура `"msft-xvd"` (`SCARLETT_XVD_MAGIC_LE64 = 0x6476782D7466736D`), `VolumeFlags`, `FormatVersion`, `DriveSize`, `ContentType`, дескрипторы XVC-регионов и флаги шифрования.
  - **Правило отказа**: если в заголовке не установлен `SCARLETT_XVD_VOLUME_FLAG_ENCRYPTION_DISABLED` (`0x02`) или обнаружены зашифрованные XVC-регионы/ненулевой key slot -> `SCARLETT_LOADER_UNIX_MOUNT_PACKAGE` устанавливает `out_is_encrypted = 1` и немедленно возвращает **`SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED`**.

#### Память (`xmem.dll` + `kernelx.dll` + `xmem_unix.so`)
- **Фиксированные 64-битные виртуальные окна**:
  - **`MEM_GRAPHICS`**: `0x0000000100000000 .. 0x000000FFFFFFFFFF` (`SCARLETT_MEM_GRAPHICS_BASE .. SCARLETT_MEM_GRAPHICS_END`, GPU-видимая и UMA-адресуемая память).
  - **`MEM_TITLE`**: `0x0000040000000000 .. 0x000007FFFFFFFFFF` (`SCARLETT_MEM_TITLE_BASE .. SCARLETT_MEM_TITLE_END`, системная память тайтла).
  - Поддерживаемые размеры страниц: `4 KiB`, `64 KiB`, `2 MiB`, `4 MiB`.
- **Профили бюджетов памяти (`PROTON_XBOX_MODEL`)**:
  - `anaconda` (Xbox Series X): **13.5 GiB** (`14,495,514,624` байт; 10.0 GiB fast + 3.5 GiB standard).
  - `lockhart` (Xbox Series S): **8.5 GiB** (`9,126,805,504` байт; 8.0 GiB fast + 0.5 GiB standard).
  - `devkit` (XSX DevKit): **40.0 GiB** (`42,949,672,960` байт).
  - Превышение бюджета профиля возвращает `SCARLETT_ERR_XMEM_BUDGET_EXCEEDED`.
- **Инварианты физического алиасинга**:
  - **RO/RW алиасинг — РАЗРЕШЁН**: одна и та же физическая страница (`PFN`) может быть отображена одновременно в два виртуальных адреса с правами `SCARLETT_MEM_ACCESS_READONLY` и `SCARLETT_MEM_ACCESS_READWRITE` при совпадении атрибутов кэширования.
  - **Конфликт cache-атрибутов — ОШИБКА**: попытка отобразить `PFN` с атрибутом кэширования (`WriteBack`, `WriteCombine`, `Uncached`, `GpuCoherent`), отличающимся от атрибута исходного выделения или существующего алиаса, **обязана** возвращать **`SCARLETT_ERR_XMEM_CACHE_CONFLICT`**.
  - **Cross-allocation map/unmap — ОШИБКА**: попытка `Map`/`Unmap` диапазона, пересекающего границу двух разных виртуальных резерваций или физических выделений, **обязана** возвращать **`SCARLETT_ERR_XMEM_CROSS_ALLOCATION`**.
  - Попытка освободить физические страницы при наличии активных виртуальных отображений возвращает **`SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED`**.
- **3-уровневый UMA-мост (`xmem_unix.so` <-> Vulkan GPU)**:
  - **Tier A (ReBAR / DMA-BUF Zero-Copy)**: требует large/Resizable BAR и `VK_KHR_external_memory_fd` + `VK_EXT_external_memory_dma_buf`. Страницы `MEM_GRAPHICS` экспортируются через DMA-BUF fd напрямую в `VkDeviceMemory` без промежуточных копий.
  - **Tier B (Host Memory Import)**: при недоступности Tier A проверяется `VK_EXT_external_memory_host` (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT`) с соблюдением `minImportedHostPointerAlignment`.
  - **Tier C (`userfaultfd` `UFFDIO_WRITEPROTECT` Fallback)**: универсальный фоллбэк (работает на любом Vulkan GPU, включая дискретные GPU с узким BAR): CPU-видимое отображение `MEM_GRAPHICS` защищается `UFFDIO_WRITEPROTECT`; обработчик page-fault фиксирует грязные страницы в битмапе и снимает защиту на запись. Перед **каждым** `ExecuteCommandLists` `d3d12_xs` вызывает `SCARLETT_XMEM_UNIX_UFFD_FLUSH_DIRTY`, атомарно копируя грязные диапазоны в `DEVICE_LOCAL` VRAM и повторно взводя `UFFDIO_WRITEPROTECT`.

---

### 3.3 Подсистема 3: RDNA2 -> SPIR-V 1.5/1.6 Lifter (`vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/`)

Интегрируется в подпроект `dxil-spirv` внутри `vkd3d-proton`. Обеспечивает два детерминированных пути трансляции:

1. **Путь A (DXIL Fast Path)**:
   - При наличии DXIL-чанка в контейнере выполняется детерминированная очистка/нормализация консольных чанков и метаданных (`strip XBOX / XS2M`), трансляция Xbox-специфичных resource bindings и передача чистого DXIL в штатный конвейер `dxil-spirv`.
2. **Путь B (Native RDNA2 / GFX10.3 Binary Lifting Path)**:
   - Входные контейнеры без DXIL: сигнатуры **`XS2`**, **`FSSM`**, **`PS\x00\x09`**, **`SGD5`**.
   - **Стадия 1 — GFX10.3 ISA Decoder**: декодирование полных наборов скалярных (`SOP1/SOP2/SOPK/SOPC/SOPP/SMEM`), векторных (`VOP1/VOP2/VOP3/VOP3P/VOPC/VINTERP`), локальной памяти (`DS`), буферных/текстурных (`MUBUF/MTBUF/MIMG/FLAT`), экспорта (`EXP`) и RT-инструкций (`image_bvh_intersect_ray` / `image_bvh64_intersect_ray`).
   - **Стадия 2 — SSA IR & CFG Structurization**: перевод `SGPR`/`VGPR`/`EXEC`/`VCC`/`SCC` в SSA-форму, восстановление структурированного CFG, совместимого со SPIR-V 1.5/1.6.
   - **Стадия 3 — 4-ступенчатый адаптивный эмиттер SPIR-V 1.5/1.6** (режим задаётся `PROTON_XBOX_SHADER_MODE=exact|adaptive|fast_f32`):
     1. **GPU-микропробы через `VkBuffer`**: при первом запуске на конкретном GPU/драйвере исполняются compute-микропробы с записью результатов в `VkBuffer` для эмпирического измерения NaN-пропагации, denorm preserve/flush (`VK_KHR_shader_float_controls` / `VK_KHR_shader_float_controls2`), `min`/`max` с NaN, 16-bit packed math и реального `subgroupSize`. Профиль кэшируется по ключу `(vendorID, deviceID, driverVersion)`.
     2. **Анализ `computeKnownFPClass`**: статический SSA-анализ доказанности отсутствия NaN/Inf/субнормалей позволяет в режиме `adaptive` безопасно опускать тяжёлую программную эмуляцию RDNA2-особенностей (`v_med3`, `v_min3`, clamping, FMA), сохраняя полную бит-точность в `exact` и агрессивную FP32-оптимизацию в `fast_f32`.
     3. **Адаптивный Wave64-on-Wave32**: при исполнении Wave64-шейдеров на Wave32 GPU (NVIDIA Warp32, RDNA Wave32, Intel SIMD16/32) применяются 3 стратегии по приоритету: (a) нативный Wave64 через `VK_EXT_subgroup_size_control` (AMD RADV); (b) статическое сужение до Wave32 при доказанной независимости полуволн; (c) спаренная виртуальная эмуляция Wave64 через `Workgroup` shared memory (LDS) + `OpControlBarrier` для кросс-полуволновых операций (`s_ballot`, `v_readlane`, `ds_swizzle`).
     4. **BVH4 Ray Tracing**: аппаратная трансляция `image_bvh*_intersect_ray` в **`VK_KHR_ray_query`** при аппаратной поддержке, и автоматический **BDA fallback** (программный обход RDNA2 BVH4-узлов через `VK_KHR_buffer_device_address` / `PhysicalStorageBuffer`) на GPU без `VK_KHR_ray_query` или при прямом манипулировании тайтлом сырыми узлами BVH4.
   - В QA- и debug-профилях каждый сгенерированный SPIR-V модуль обязан проходить `spirv-val` с exit code `0`.

---

### 3.4 Подсистема 4: `d3d12_xs.dll` (`vkd3d-proton/libs/d3d12_xs/`) + `xg_xs.dll` + `XboxBvhBuilder.dll` + `pixevt.dll`

- **`d3d12_xs.dll` (расширение `vkd3d-proton`)**:
  - **`CreatePlacedResourceX`**: прямое размещение ресурса по 64-битному GPU VA из окна `MEM_GRAPHICS` без предварительного `ID3D12Heap`, с расчётом физического layout через `xg_xs.dll` и привязкой к UMA-мосту `xmem_unix.so`.
  - **Консольные очереди команд**:
    - `0x2` (`SCARLETT_D3D12_QUEUE_TYPE_DMA`): выделенная DMA/Copy очередь -> Vulkan Transfer Queue (фоллбэк на Graphics/Compute).
    - `0x10` (`SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS`): консольная асинхронная compute-очередь -> Vulkan Async Compute Queue (фоллбэк на Graphics+Compute).
  - **Дескрипторная подсистема и динамическая автозащита**:
    - Базовый путь: `VK_EXT_descriptor_heap` (плоские GPU-адресуемые кучи дескрипторов).
    - **Автозащита от NVIDIA `610.xx` и лимита `1,000,000` (1M) дескрипторов**: при инициализации устройства динамически проверяются `VkPhysicalDeviceDriverProperties` и лимиты расширений. Если драйвер принадлежит проблемной ветке NVIDIA `610.xx` **или** запрашиваемый/аппаратный пул дескрипторов превышает безопасный порог `1,000,000` (`SCARLETT_D3D12_DESC_HEAP_SAFE_LIMIT`) -> выполняется автоматический бесшовный fallback на **`VK_EXT_descriptor_buffer`** (и далее на `VK_EXT_descriptor_indexing` с `updateAfterBind`, если `VK_EXT_descriptor_buffer` отсутствует).
  - **`PresentX` и `WaitFrameEventX`**:
    - `PresentX`: поддержка 1 или 2 независимых плоскостей (primary game plane + secondary UI/HUD plane), вывод **HDR10 PQ** (`VK_COLOR_SPACE_HDR10_ST2084_EXT` + `VK_EXT_hdr_metadata` SMPTE ST.2086) и неблокирующий отвод кадра для GameDVR.
    - `WaitFrameEventX`: привязка к кадровым событиям (`ORIGIN` / `PRESENT`) через `VK_GOOGLE_display_timing` / `VK_KHR_present_wait` + `VK_KHR_present_id` с точным фоллбэком на `clock_nanosleep(CLOCK_MONOTONIC)`.
  - **SFS (Sampler Feedback Streaming) и `vkQueueBindSparse` coalescing**:
    - Поддержка reserved/tiled ресурсов и SFS mip-feedback карт.
    - Все пакетные `UpdateTileMappings` накапливаются и объединяются в смежные диапазоны перед отправкой минимального числа `vkQueueBindSparse` вызовов (coalescing) на границе отправки командного списка.
- **`xg_xs.dll` (RDNA2/GFX10.3 Surface Address Calculator)**:
  - Режимы тайлинга: **`SW_4KB_D`**, **`SW_4KB_S`**, **`SW_64KB`** (включая `SW_64KB_S`, `SW_64KB_D`, `SW_64KB_R_X`, `SW_64KB_D_X`, `SW_64KB_S_X`, `SW_LINEAR`).
  - Глубина форматов: **от 8 bpp до 128 bpp** (включая BC1..BC7, depth/stencil, MSAA, 2D/3D/Array, полные mip-цепочки).
  - Строго 64-битная адресная арифметика (`uint64_t`) для поддержки поверхностей и виртуальных пулов объёмом **> 4 GiB** (переполнение детектируется и возвращает `SCARLETT_ERR_XG_SURFACE_OVERFLOW`).
- **`XboxBvhBuilder.dll`**:
  - Построение и рефит BLAS/TLAS поверх `VK_KHR_acceleration_structure`, либо генерация RDNA2-совместимого BVH4 layout в GPU-буфере для BDA-фоллбэка.
- **`pixevt.dll`**:
  - Проброс `PIXBeginEvent`, `PIXEndEvent`, `PIXSetMarker` в `VK_EXT_debug_utils` (`vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT` / `vkCmdInsertDebugUtilsLabelEXT`) с нулевыми накладными расходами при отключённых debug labels.

#### Матрица динамической проверки и фоллбэков Vulkan-расширений (AMD RADV / NVIDIA 595+/610+ / Intel ANV)

| Возможность | Основное расширение | Динамический Fallback #1 | Динамический Fallback #2 | Поведение по вендорам |
| :--- | :--- | :--- | :--- | :--- |
| **Дескрипторы** | `VK_EXT_descriptor_heap` | `VK_EXT_descriptor_buffer` | `VK_EXT_descriptor_indexing` | **NVIDIA 610.xx** или **>1M дескрипторов**: принудительный переход на `VK_EXT_descriptor_buffer`. **AMD RADV / Intel ANV**: выбор по `vkEnumerateDeviceExtensionProperties` + лимиту 1M. |
| **UMA-мост (`MEM_GRAPHICS`)** | Tier A: `VK_KHR_external_memory_fd` + `VK_EXT_external_memory_dma_buf` (ReBAR) | Tier B: `VK_EXT_external_memory_host` | Tier C: `userfaultfd` `UFFDIO_WRITEPROTECT` + flush перед `ExecuteCommandLists` | **RADV/ANV с ReBAR**: Tier A. **Без ReBAR с host-import**: Tier B. **NVIDIA 595+/610+ без ReBAR или узкий BAR**: Tier B/C. |
| **Wave64 шейдеры** | `VK_EXT_subgroup_size_control` (native Wave64) | Статическое сужение Wave32 | Виртуальный Wave64 через LDS + `OpControlBarrier` | **AMD RADV**: нативный Wave64. **NVIDIA 595+/610+** (Warp32) и **Intel ANV** (SIMD16/32): сужение / LDS-эмуляция. |
| **BVH4 Ray Tracing** | `VK_KHR_ray_query` + `VK_KHR_acceleration_structure` | Программный BVH4 обход через `VK_KHR_buffer_device_address` (BDA) | — | **RADV / NVIDIA 595+/610+ / ANV (Xe-HPG+)**: `VK_KHR_ray_query` где валидно, иначе бесшовный BDA fallback. |
| **DirectStorage Deflate** | `VK_EXT_memory_decompression` | GPU Compute `vkdeflate` | CPU `zlib-ng` / `libdeflate` | **NVIDIA 595+/610+**: проверка `VK_EXT_memory_decompression` -> `vkdeflate` -> CPU. **RADV / ANV**: `vkdeflate` -> CPU. |
| **PresentX HDR10 / Pacing** | `VK_EXT_swapchain_colorspace` + `VK_EXT_hdr_metadata` + `VK_GOOGLE_display_timing` | `VK_KHR_present_wait` + `VK_KHR_present_id` | SDR tonemap + `clock_nanosleep(CLOCK_MONOTONIC)` | Единое динамическое зондирование для RADV, NVIDIA 595+/610+, ANV. |

---

### 3.5 Подсистема 5: `dstorage_xs.dll` + `dstorage_xs_unix.so` (`dstorage_xs.so`)

- **Точная 64-битная раскладка `DSTORAGE_REQUEST_OPTIONS`** (`scarlett_dstorage_request_options_t`, 8 байт, little-endian):

| Поле | Биты | Ширина | Назначение |
| :--- | :---: | :---: | :--- |
| `ZlibDecompress` | `0` | `1` | Декомпрессия потока Zlib/Deflate |
| `BcpackMode` | `1..4` | `4` | Режим BCPack (`0` = выкл., `1..15` = формат блока) |
| `SwizzleMode` | `5..20` | `16` | Режим тайлинга `XG_SWIZZLE_MODE` для `xg_xs` / GPU retile |
| `DestinationIsPhysicalPages` | `21` | `1` | Назначение адресуется физическими страницами `MEM_GRAPHICS` |
| `SourceType` | `22` | `1` | `0` = файл, `1` = буфер в памяти |
| `SourceIsPhysicalPages` | `23` | `1` | Источник адресуется физическими страницами |
| `Reserved` | `24..63` | `40` | Должно быть `0` (иначе `SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS`) |

- **Конвейер I/O и декомпрессии**:
  - Асинхронное чтение: Linux **`io_uring`** (`IORING_OP_READ`, выровненный `O_DIRECT` где допустимо), с автоматическим фоллбэком на пул `pread` при ограничениях seccomp.
  - Декомпрессия Zlib/Deflate: иерархия `VK_EXT_memory_decompression` -> GPU compute **`vkdeflate`** -> CPU **`zlib-ng` / `libdeflate`**.
  - **Плагинный `IBcpackDecoder` ABI** (`scarlett/include/scarlett_abi_common.h`):
    - Путь к пользовательскому плагину передаётся через `PROTON_XBOX_BCPACK_PLUGIN=<path.so>` и загружается в `dstorage_xs_unix.so` через `SCARLETT_DSTORAGE_UNIX_LOAD_BCPACK_PLUGIN`.
    - Точка входа плагина: `scarlett_hresult_t scarlett_bcpack_create_decoder(uint32_t requested_abi_version, scarlett_bcpack_decoder_t **out_decoder)` (версия ABI `SCARLETT_BCPACK_PLUGIN_ABI_VERSION == 1`).
    - При `BcpackMode != 0` без загруженного плагина возвращается явная ошибка **`SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING`** (запрещён тихий пропуск с повреждением текстур).

---

### 3.6 Подсистема 6: `xgameruntime.dll` (22 домена, `XTaskQueue`, `XGameSave`, `libHttpClient`/`XCurl`)

- **22 домена `XGameRuntimeFeature`** (`scarlett_game_runtime_feature_t`, индексы `0..21`):
  `0: XAccessibility`, `1: XAppCapture`, `2: XAsync`, `3: XAsyncProvider`, `4: XDisplay`, `5: XGame`, `6: XGameInvite`, `7: XGameSave`, `8: XGameUI`, `9: XLauncher`, `10: XNetworking`, `11: XPackage`, `12: XPersistentLocalStorage`, `13: XSpeechSynthesizer`, `14: XStore`, `15: XSystem`, `16: XTaskQueue`, `17: XThread`, `18: XUser`, `19: XError`, `20: XGameEvent`, `21: XGameStreaming`.
- **`XTaskQueue` и `XAsync`**:
  - Поддержка всех 4 режимов портов (`ThreadPool`, `SerializedThreadPool`, `Manual`, `Immediate`), интеграция с `NTSYNC`, строгое соблюдение жизненного цикла отмены (`XAsyncCancel`) и однократного завершения callback.
- **`XGameSave` — все 15 официальных кодов ошибок GDK (`0x80830001..0x8083000F`)**:
  - `0x80830001` `E_GS_INVALID_CONTAINER_NAME`
  - `0x80830002` `E_GS_NO_ACCESS`
  - `0x80830003` `E_GS_OUT_OF_LOCAL_STORAGE`
  - `0x80830004` `E_GS_USER_CANCELED`
  - `0x80830005` `E_GS_UPDATE_TOO_BIG` (лимит блоба 16 МБ)
  - `0x80830006` `E_GS_QUOTA_EXCEEDED`
  - `0x80830007` `E_GS_PROVIDED_BUFFER_TOO_SMALL`
  - `0x80830008` `E_GS_BLOB_NOT_FOUND`
  - `0x80830009` `E_GS_NO_SERVICE_CONFIGURATION`
  - `0x8083000A` `E_GS_CONTAINER_NOT_IN_SYNC`
  - `0x8083000B` `E_GS_CONTAINER_SYNC_FAILED`
  - `0x8083000C` `E_GS_USER_NOT_REGISTERED_IN_SERVICE`
  - `0x8083000D` `E_GS_HANDLE_EXPIRED`
  - `0x8083000E` `E_GS_ASYNC_FUNCTION_REQUIRED` (синхронный вызов в потоке `XThreadIsTimeSensitive() == true`)
  - `0x8083000F` `E_GS_PROVIDER_MISMATCH` (смешивание `XGameSave` и `XGameSaveFiles`)
  - Хранилище: транзакционная атомарная запись (`tmp` + `fsync` + `rename`) в `pfx/drive_c/users/steamuser/AppData/Local/ScarlettSaves/<TitleId>/` через `SCARLETT_LOADER_UNIX_GAMESAVE_COMMIT_BLOB`.
- **`XThreadIsTimeSensitive()` / `XThreadSetTimeSensitive(bool)`**:
  - Локальное состояние потока (TLS) + запрос повышения приоритета через `SCARLETT_LOADER_UNIX_SET_TIME_SENSITIVE` (RTKit / `nice` при наличии прав, с обязательным сохранением логического флага даже без привилегий). Использование блокирующих синхронных `XGameSave`-вызовов при `XThreadIsTimeSensitive() == true` возвращает `E_GS_ASYNC_FUNCTION_REQUIRED`.
- **`libHttpClient` / `XCurl` и сетевые режимы (`PROTON_XBOX_NET_MODE`)**:
  - `offline` (по умолчанию): детерминированная блокировка исходящих соединений с кодом `SCARLETT_ERR_NET_OFFLINE_BLOCKED`.
  - `lan`: разрешение только loopback (`127.0.0.0/8`, `::1`), RFC1918 (`10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`) и link-local (`169.254.0.0/16`, `fe80::/10`); внешние адреса блокируются с `SCARLETT_ERR_NET_LAN_POLICY_BLOCKED`.
  - `passthrough`: полный проброс HTTP/HTTPS/WebSocket с верификацией TLS через системный пул сертификатов (`SSL_CERT_FILE` / `SSL_CERT_DIR`).

---

### 3.7 Подсистема 7: Аудио, ввод, медиа и апскейлинг

- **`xaudio2_9.dll` (Ordinal `#1`)**:
  - Обязательный экспорт `XAudio2Create` по ординалу `#1` и по имени.
  - Аудиовывод через `audio_scarlett_unix.so` (FAudio / PipeWire / PulseAudio), поддержка PCM, IEEE float, ADPCM, Opus и XMA2, а также X3DAudio.
- **`AcpHal.dll`**:
  - Программная реализация Audio Co-Processor (XMA2 декодирование, sample-rate conversion, DSP-контексты) через `SCARLETT_AUDIO_UNIX_ACPHAL_DECODE` (FFmpeg/SIMD в `audio_scarlett_unix.so`).
- **`GameInput.dll` (Ordinal `#1`, 4-motor rumble)**:
  - Обязательный экспорт `GameInputCreate` по ординалу `#1` и по имени.
  - Полная поддержка `IGameInput` / `IGameInputReading` / `IGameInputDevice` поверх `evdev`/`hidraw` в `audio_scarlett_unix.so`, включая **4-моторную виброотдачу** (`low_frequency`, `high_frequency`, `left_trigger`, `right_trigger` в `scarlett_input_rumble_params_t`, совместимо с `xpadneo`/`xone`/HID impulse triggers).
- **`Bink2Scarlett.dll`**:
  - Clean-room медиадекодер `.bk2` поверх `ffmpeg`/`dav1d` + аппаратное ускорение VA-API/Vulkan Video (`SCARLETT_AUDIO_UNIX_BINK2_DECODE`) с прямым выводом плоскостей Y/UV в текстуры `d3d12_xs`.
- **`amd_fidelityfx_loader_xbox.dll` & `amd_fidelityfx_upscaler_xbox.dll` (`amd_fidelityfx_*_xbox.dll`)**:
  - Трансляция вызовов Xbox FidelityFX SDK (FSR 2 / 3 / 3.1 / Frame Generation) в открытые FSR Vulkan-шейдеры с динамической проверкой FP16/Wave64 и безопасным фоллбэком без проприетарных консольных бинарей.

---

### 3.8 Подсистема 8: QA Harness & `ScarlettArena-Linux` (6 стресс-сцен и Exit Gates Phase 0..7)

**`ScarlettArena-Linux`** — собственный open-source Clean-Room интеграционный PE64-тайтл (с `MicrosoftGame.Config` / `TargetDeviceFamily=XboxGen9`), верифицирующий все 8 подсистем в 6 стресс-сценах:

1. **Scene 1 — XDL Dispatch, VFS & XMem Aliasing Torture**:
   - Вызов `QueryApiImpl` по Win64 register ABI для обоих 32-слотовых GUID (`{073B7DCB-...}` и `{37E56907-...}`).
   - Проверка CoW-записи поверх read-only `GAME:` и негативный тест зашифрованного XVC (`SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED`).
   - Тестирование `MEM_GRAPHICS` / `MEM_TITLE`, RO/RW алиасинга, негативных тестов `SCARLETT_ERR_XMEM_CACHE_CONFLICT` и `SCARLETT_ERR_XMEM_CROSS_ALLOCATION`, а также всех 3 уровней UMA-моста (Tier A / B / C `userfaultfd`).
2. **Scene 2 — RDNA2 Lifter, Microprobes & Wave64 Matrix**:
   - Шейдеры Пути A (DXIL strip `XBOX`/`XS2M`) и Пути B (`XS2`/`FSSM`/`PS\x00\x09`/`SGD5`) -> SPIR-V 1.5/1.6 + `spirv-val`.
   - Верификация GPU-микропроб в `VkBuffer`, `computeKnownFPClass`, адаптивного Wave64-on-Wave32, BVH4 RT (`VK_KHR_ray_query` и BDA fallback) и сравнение golden-image для `exact`/`adaptive`/`fast_f32`.
3. **Scene 3 — D3D12_XS Descriptor Storm, Multi-Queue & PresentX**:
   - `CreatePlacedResourceX`, параллельная работа очередей `0x2` (DMA) и `0x10` (async compute).
   - Стресс-тест >1M дескрипторов (проверка автопереключения `VK_EXT_descriptor_heap` -> `VK_EXT_descriptor_buffer`).
   - `PresentX` (1 и 2 плоскости, HDR10 PQ + GameDVR) и точность `WaitFrameEventX`.
4. **Scene 4 — XG_XS >4 GiB Tiling & SFS Sparse Coalescing**:
   - Проверка `SW_4KB_D`, `SW_4KB_S`, `SW_64KB` для всех форматов `8..128 bpp` и виртуального ресурсного пула `> 4 GiB`.
   - Стресс-тест SFS с проверкой агрегации батчей `vkQueueBindSparse` coalescing.
5. **Scene 5 — DirectStorage_XS Streaming & Pluggable BCPack**:
   - Потоковое чтение через `io_uring`, GPU `vkdeflate` / `VK_EXT_memory_decompression` + CPU `zlib-ng`/`libdeflate`, GPU retiling по `SwizzleMode`.
   - Позитивный тест тестового Clean-Room `IBcpackDecoder`-плагина и негативный тест отсутствия плагина (`SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING`).
6. **Scene 6 — Runtime, XGameSave, Audio, 4-Motor Input, Media & Strict Trap**:
   - `XGameRuntimeFeature` (22 домена), `XTaskQueue`/`XAsync`, покрытие всех 15 кодов `E_GS_*` (`0x80830001..0x8083000F`), `XThreadIsTimeSensitive()`, `libHttpClient`/`XCurl` (`offline`/`lan`/`passthrough`).
   - `xaudio2_9` (#1), `AcpHal`, `GameInput` (#1, 4-motor rumble), `Bink2Scarlett`, `amd_fidelityfx_*_xbox`.
   - Тест `PROTON_XBOX_STRICT=1` (ожидаемый контролируемый останов с полным дампом `scarlett_stub_context_t`).

#### Exit Gates (Phase 0..7)

| Фаза | Наименование | Обязательные критерии выхода (Exit Gate) |
| :---: | :--- | :--- |
| **Phase 0** | Контракты, заголовки и каркас Proton 11.0 | `ABI_CONTRACTS.md`, 4 заголовка проходят `gcc -std=c11 -Wall -Wextra -Werror` и `g++ -std=c++20 -Wall -Wextra -Werror`, валидный `scarlett_app_db.toml`, актуальный `STATE_LEDGER.json`, каркас CMake/Meson/`Makefile.in`/`proton`/`default_pfx.py`, 0 регрессий ПК-Proton, 6-пунктовый аудит пройден. |
| **Phase 1** | `scarlett-xdl`, auto-stub thunk, Loader/VFS/XVD | Компилятор `scarlett-xdl` генерирует 32-слотовые vtable для обоих GUID, работает Win64 `QueryApiImpl`, `PROTON_XBOX_STRICT=1` ловит заглушки с `RCX/RDX/R8/R9` и RVA, CoW-оверлей `GAME:` и отказ `SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED` на зашифрованных XVC покрыты тестами. |
| **Phase 2** | `xmem.dll` + `kernelx.dll` + `xmem_unix.so` | Резервация `MEM_GRAPHICS`/`MEM_TITLE`, соблюдение бюджетов `anaconda`/`lockhart`/`devkit`, инварианты RO/RW, `CACHE_CONFLICT`, `CROSS_ALLOCATION`, работа Tier A/B/C UMA-моста с `UFFDIO_WRITEPROTECT` flush перед `ExecuteCommandLists`. |
| **Phase 3** | RDNA2 -> SPIR-V 1.5/1.6 Lifter | Путь A (DXIL strip) и Путь B (`XS2`/`FSSM`/`PS\x00\x09`/`SGD5`), GPU-микропробы, `computeKnownFPClass`, адаптивный Wave64-on-Wave32, BVH4 `VK_KHR_ray_query` + BDA fallback, 100% прохождение `spirv-val`. |
| **Phase 4** | `d3d12_xs`, `xg_xs`, `XboxBvhBuilder`, `pixevt` | `CreatePlacedResourceX`, очереди `0x2`/`0x10`, `VK_EXT_descriptor_heap` + автозащита NVIDIA `610.xx`/1M -> `VK_EXT_descriptor_buffer`, `PresentX` (1/2 плоскости HDR10 PQ + GameDVR), `WaitFrameEventX`, `xg_xs` `8..128 bpp` >4 GiB, SFS coalescing. |
| **Phase 5** | `dstorage_xs.dll` + `dstorage_xs_unix.so` | Точный разбор `DSTORAGE_REQUEST_OPTIONS`, `io_uring`, GPU `vkdeflate`/`VK_EXT_memory_decompression` + CPU `zlib-ng`/`libdeflate`, плагин `IBcpackDecoder` и негативный тест отсутствия плагина. |
| **Phase 6** | `xgameruntime`, аудио, ввод, видео, FSR | 22 домена `XGameRuntimeFeature`, `XTaskQueue`/`XAsync`, 15 кодов `XGameSave`, `XThreadIsTimeSensitive`, `libHttpClient`/`XCurl`, `xaudio2_9` (#1), `AcpHal`, `GameInput` (#1, 4-motor), `Bink2Scarlett`, `amd_fidelityfx_*_xbox`. |
| **Phase 7** | Полная верификация `ScarlettArena-Linux` | Прохождение всех 6 стресс-сцен на матрице драйверов (AMD RADV, NVIDIA 595+/610+, Intel ANV), длительный soak-тест без утечек памяти/дескрипторов, 0 регрессий обычного Proton 11.0. |
