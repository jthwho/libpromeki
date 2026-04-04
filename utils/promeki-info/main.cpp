/*****************************************************************************
 * promeki-info
 *
 * Prints information about the promeki library build.
 *
 * promeki project root folder.
 *****************************************************************************/

#include <cstdio>
#include <promeki/buildinfo.h>

using namespace promeki;

int main(int argc, char *argv[]) {
        const BuildInfo *info = getBuildInfo();
        std::printf("promeki library info\n");
        std::printf("  Name:       %s\n", info->name);
        std::printf("  Version:    %s\n", info->version);
        std::printf("  Build Date: %s\n", info->date);
        std::printf("  Build Time: %s\n", info->time);
        std::printf("  Build Type: %s\n", info->type);
        std::printf("  Build Host: %s\n", info->hostname);
        std::printf("  Repo Ident: %s\n", info->repoident);
        return 0;
}
