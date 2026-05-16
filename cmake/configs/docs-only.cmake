# Docs-only preset.
#
# Skip every library, executable, and third-party dependency; configure
# only the Doxygen `docs` target.  Used by the GitHub Actions
# documentation workflow so it can build docs from a checkout that did
# NOT pull any submodules.
#
# Equivalent to passing -DPROMEKI_DOCS_ONLY=ON on the command line.

promeki_config_option(PROMEKI_DOCS_ONLY ON)
promeki_config_option(PROMEKI_BUILD_DOCS ON)
