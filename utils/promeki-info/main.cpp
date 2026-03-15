/*****************************************************************************
 * promeki-info
 *
 * Prints information about the promeki library build.
 *
 * promeki project root folder.
 *****************************************************************************/

#include <iostream>
#include <promeki/core/buildinfo.h>

using namespace promeki;

int main(int argc, char *argv[]) {
        const BuildInfo *info = getBuildInfo();
        std::cout << "promeki library info" << std::endl;
        std::cout << "  Name:       " << info->name << std::endl;
        std::cout << "  Version:    " << info->version << std::endl;
        std::cout << "  Build Date: " << info->date << std::endl;
        std::cout << "  Build Time: " << info->time << std::endl;
        std::cout << "  Build Type: " << info->type << std::endl;
        std::cout << "  Build Host: " << info->hostname << std::endl;
        std::cout << "  Repo Ident: " << info->repoident << std::endl;
        return 0;
}
