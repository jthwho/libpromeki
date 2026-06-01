# PromekiGplComponents.cmake
#
# Reusable GPL-compliance notification mechanism.
#
# Some optional components of libpromeki are licensed under the GPL (e.g.
# libx264; libx265 is planned).  Enabling any of them makes the resulting
# libpromeki build, as a whole, a work governed by the GPL.  This module
# surfaces that loudly at configure time so a distributor can't enable a
# GPL component by accident.
#
# Usage:
#
#   1. include() this file once, early, from the top-level CMakeLists.
#   2. Inside each GPL-licensed dependency's enable guard, register it:
#
#          if(PROMEKI_ENABLE_X264)
#              promeki_register_gpl_component("libx264" "GPL-2.0-or-later")
#          endif()
#
#   3. After ALL PROMEKI_ENABLE_* flags have resolved (and before config.h
#      is generated), call promeki_emit_gpl_notice() once.  It prints the
#      notice (if any GPL component is enabled) and sets the cache variable
#      PROMEKI_GPL_BUILD (ON/OFF), which config.h.in mirrors so an
#      application can report the GPL posture at runtime.

# Register one GPL-licensed component as enabled in this build.  Idempotent
# enough for our purposes — duplicate registrations just repeat in the list.
function(promeki_register_gpl_component name license)
    set_property(GLOBAL APPEND PROPERTY PROMEKI_GPL_COMPONENTS "${name} (${license})")
endfunction()

# Emit the GPL compliance notice (once) and set PROMEKI_GPL_BUILD.
function(promeki_emit_gpl_notice)
    get_property(_gpl GLOBAL PROPERTY PROMEKI_GPL_COMPONENTS)
    if(_gpl)
        set(PROMEKI_GPL_BUILD ON CACHE INTERNAL "A GPL-licensed component is enabled in this build")
        list(JOIN _gpl "\n     - " _gpl_list)
        message(WARNING
            "\n"
            "============================================================\n"
            "  GPL COMPLIANCE NOTICE\n"
            "============================================================\n"
            "  One or more GPL-licensed components are ENABLED in this\n"
            "  build of libpromeki:\n"
            "     - ${_gpl_list}\n"
            "\n"
            "  As a result, the resulting libpromeki build is a work\n"
            "  subject to the GNU General Public License, v2.0 or later.\n"
            "  If you distribute it you must comply with the GPL,\n"
            "  including making complete corresponding source available.\n"
            "  To produce a non-GPL build, reconfigure with the listed\n"
            "  -DPROMEKI_ENABLE_*=OFF flag(s).\n"
            "  See THIRD-PARTY-LICENSES (\"GPL-Licensed Components\").\n"
            "============================================================")
    else()
        set(PROMEKI_GPL_BUILD OFF CACHE INTERNAL "A GPL-licensed component is enabled in this build")
    endif()
endfunction()
