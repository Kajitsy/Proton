#ifndef SCARLETT_ARENA_SCENES_HPP
#define SCARLETT_ARENA_SCENES_HPP

#include "arena_telemetry.hpp"

namespace scarlett::arena {

bool run_scene1_uma_aliasing(ArenaTelemetry& tel);
bool run_scene2_rdna2_lifter(ArenaTelemetry& tel);
bool run_scene3_dstorage_velocity(ArenaTelemetry& tel);
bool run_scene4_dx12_ultimate_aaa(ArenaTelemetry& tel);
bool run_scene5_presentx_pacing(ArenaTelemetry& tel);
bool run_scene6_runtime_audio_vfs(ArenaTelemetry& tel);

} // namespace scarlett::arena

#endif /* SCARLETT_ARENA_SCENES_HPP */
