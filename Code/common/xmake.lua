
local cBuildVersion = get_config("build_version")

target("CommonLib")
    if cBuildVersion and cBuildVersion ~= "" then
        add_configfiles("BuildInfo.h.in", {variables = {GIT_TAG = cBuildVersion}})
    else
        add_configfiles("BuildInfo.h.in")
    end
    set_kind("static")
    set_group("common")
    add_includedirs(".", "../", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")
    add_packages("tiltedcore", "hopscotch-map")
