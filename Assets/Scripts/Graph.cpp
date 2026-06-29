#include "ScriptAPI.h"
#include <cmath>

SCRIPT_EXPORT void ScriptUpdate(const ScriptContext* ctx, float dt)
{
    (void)dt;
    ctx->spawnPointLight(ScriptVec3{ 0.0f, 2.0f, 0.0f }, 5.0f, ScriptVec3{ 1.0f, 0.6f, 0.2f }, (((ctx->isKeyDown("B") != 0) == false) ? 25.0f : 5.0f));
}

//@graph 1
//@node 0 EventUpdate 92 231
//@node 1 SpawnPointLight 720 284
//@node 2 IsKeyDown -25 343
//@node 3 Conditional 333 291
//@pin 1 1 ScriptVec3{ 0.0f, 2.0f, 0.0f }
//@pin 1 2 5.0f
//@pin 1 3 ScriptVec3{ 1.0f, 0.6f, 0.2f }
//@pin 1 4 60.0f
//@pin 2 0 "B"
//@pin 3 1 0.0f
//@pin 3 2 false
//@pin 3 3 25.0f
//@pin 3 4 5.0f
//@enum 3 2
//@link 3 1 1 4
//@link 0 0 3 0
//@link 3 0 1 0
//@link 2 0 3 1
