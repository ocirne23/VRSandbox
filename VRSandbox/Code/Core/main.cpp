#include <entt/entity/registry.hpp>

import World;
import Utils.PathUtils;

int main(int argc, const char *argv[])
{
    PathUtils::setHardcodedWorkingDir();

    entt::registry registry;
    World world(registry);
    world.initialize();
    world.updateLoop();

    return 0;
}