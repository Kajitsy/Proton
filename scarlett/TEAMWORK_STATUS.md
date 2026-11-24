# Scarlett Teamwork Status

Обновлено: 2026-11-24T16:54:00Z

## Что сделано (итерация 4: горячий патч `clzero` и динамическая трассировка)
- `ccache` включен (`ENABLE_CCACHE := 1` в `build/Makefile`), инкрементальная сборка `scarlett` проходит за ~22–31s (`exit 0`).
- Горячий патч `clzero_loop` в `scarlett/xgameruntime/xgameruntime_pe.c` сработал: число `0xc000001d` упало с `4615` до `24` строк (`[scarlett-veh] Patched clzero_loop` на `0x144e8aa70/90/b0/d0/f0`).
- Прежний OOM-сбой `0x142219650` / `0x1406EB759` в 45-секундном прогоне R3 (с повторным использованием `/tmp/gta`) не воспроизвелся (`0` совпадений, в логе `16076` строк).
- `PresentX` дошел до `frame=840` (`88` строк `[scarlett-presentx]`, `hr=0x00000000`), с ненулевым `p0va` (`0x1C610000` и `0x1E5C0000`, по `31.7 MiB`).

## Захват окна `Americas`
- Окно `Americas` (`class='steam_proton'`, `ws=5`, геометрия `1610,48 1900x1022`, X11 `0x5800001`) захвачено во время работы игры:
  - `/tmp/gta_iter4_x11.png`: пустой 1-битный PNG (`1900x1022`, `375` байт).
  - `/tmp/gta_iter4_grim.png`: RGB PNG `1900x1022` (`502871` байт, `659` уникальных цветов, min `[1, 0, 0]`, max `[179, 151, 148]`). Поверхность окна прозрачна/неотрисована (виден размытый фон рабочего стола через Hyprland blur), так как поток `01a0` упал на `Frame: 0`.

## Динамическая трассировка `/tmp/gta_scarlett_loop.log`
- `[scarlett-xmem-alloc]`: `10` вызовов (резервы `24 GiB`, `0x89700000` = `2199 MiB`, `0x185c00000` = `6236 MiB`), `status=0x0`; `[scarlett-xmem-free]` / `phys`: `0`.
- `CreatePlacedResourceX`: `19` записей; `ID3D12ResourceX::Release` (`[scarlett-d3d12-res-free]`): `0`.
- `ID3D12FenceX::Signal`: `33` записи (до `0x6a4`); `GetCompletedValue`: `4` записи (до `0x6ad`); `SetEventOnCompletion`: `0`.
- `WriteGpuMemoryLabelX`: `19` записей (~1750+ вызовов до порога `512` на одном командном списке); `FlushGpuLabels`: `12` записей (`seq=1..12`).
- `GetFrameStatisticsX`: `15` записей (до `frame=0x348` = `840`).
- `dstorage_xs`: `188` записей (`OpenFile` `59`, `CreateQueue` `17`, `CreateStatusArray` `17`, `EnqueueRequest` `84`, `Submit` `9`, `EnqueueSignal`/`EnqueueSetEvent` `0`).

## Новый блокер (падение внутри `d3d12_xs.dll+0xD8A3`)
- На строке `L14920` поток `01a0` получил `EXCEPTION_ACCESS_VIOLATION` (`0xc0000005`, `info[1]=FFFFFFFFFFFFFFFF`) в `d3d12_xs.dll+0xD8A3` (`d3d12xs_cmd_list_flush_labels`), вызванной из `d3d12_xs.dll+0x4FD0` (`cmd_list_slot71_write_gpu_memory_label_x`, при `label_write_count >= 512`).
- Стек: `d3d12_xs.dll+0xD8A3` -> `d3d12_xs.dll+0x4FD0` -> `game_scarlett_master.exe+0x04ED46AA` -> `+0x02484284` -> `+0x0248412E` -> `+0x00CE03DF` -> `+0x00120CF6` -> `+0x04FD1B76` -> `+0x023CF247` -> `+0x02215ECA`.
- Причина: `FlushGpuLabels` записал `label_writes[0..11]` (`0x138ac00008..0x138ac0009c`), а в `label_writes[12]` (смещение `+0x118` внутри `ID3D12GraphicsCommandListX`, `cl=0x6ffff9245380`) оказались мусорные/чужие байты `gpu_va=0xc2c1c0bf00000001` (`rax`) и `value=0xc6c5c4c3` (`r8`) — вероятно, `label_writes` пересекается с inline-полями Xbox `ID3D12GraphicsCommandListX` за пределами `+0x50` (или не вынесен во внешнюю таблицу).
- Breakpad сохранил `t:\1cfab532-629b-469f-beb3-c02f72c3592f.dmp` и `t:\game_scarlett_master-20261124-195032-332.crash.log` (`Game State: Game Init`, `Frame: 0`, `TileHeap: used (8Mb) - total (6236Mb)`).
