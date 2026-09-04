# Source numbers select the fork's series; compatibility numbers are separate.
# Accepted release tags: 0.1.1 or minus-v0.1.1 (also with -rc.1, etc.).
file(READ "${SRC}" version_template)
foreach(component MAJOR MINOR BUGFIX)
  string(REGEX MATCH "#define[ \t]+AVS_MINUS_${component}_VER[ \t]+([0-9]+)" match "${version_template}")
  if(NOT match)
    message(FATAL_ERROR "Missing AVS_MINUS_${component}_VER in ${SRC}")
  endif()
  set(minus_${component} "${CMAKE_MATCH_1}")
endforeach()
set(AVS_MINUS_VERSION "${minus_MAJOR}.${minus_MINOR}.${minus_BUGFIX}-dev+unknown")
set(AVS_DEV_REVDATE unknown)

if(NOT GIT OR NOT EXISTS "${GIT}")
  find_package(Git QUIET)
  set(GIT "${GIT_EXECUTABLE}")
endif()

# -C also supports .git pointer files in worktrees and submodules.
function(read_git output)
  execute_process(COMMAND "${GIT}" -C "${REPO}" ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE value
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(result EQUAL 0)
    set(${output} "${value}" PARENT_SCOPE)
  else()
    set(${output} "" PARENT_SCOPE)
  endif()
endfunction()

if(EXISTS "${REPO}/.git" AND EXISTS "${GIT}")
  read_git(commit_hash rev-parse --verify HEAD)
  if(commit_hash)
    read_git(short_hash rev-parse --short=8 HEAD)
    read_git(AVS_DEV_REVDATE log -1 --format=%cs HEAD)
    set(AVS_MINUS_VERSION "${minus_MAJOR}.${minus_MINOR}.${minus_BUGFIX}-dev-g${short_hash}")

    # Exclude upstream tags and other series, even after a branch merge.
    read_git(tags tag --list)
    string(REPLACE "\n" ";" tags "${tags}")
    set(tag_args)
    foreach(tag IN LISTS tags)
      if(tag MATCHES "^(minus-v)?${minus_MAJOR}\\.${minus_MINOR}\\.[0-9]+(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$")
        list(APPEND tag_args --match "${tag}")
      endif()
    endforeach()
    if(tag_args)
      read_git(description describe --tags --first-parent --long --abbrev=8 ${tag_args} HEAD)
      if(description MATCHES "^(.*)-([0-9]+)-g([0-9a-f]+)$")
        set(release_tag "${CMAKE_MATCH_1}")
        set(commits_since_tag "${CMAKE_MATCH_2}")
        string(REGEX REPLACE "^minus-v" "" AVS_MINUS_VERSION "${release_tag}")
        if(commits_since_tag GREATER 0)
          set(AVS_MINUS_VERSION "${AVS_MINUS_VERSION}+${commits_since_tag}-g${short_hash}")
        endif()
      endif()
    endif()

  endif()
endif()

# Windows fixed version fields require four integers. Derive the base from
# the selected tag rather than potentially stale source patch numbers.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" numeric_version "${AVS_MINUS_VERSION}")
set(AVS_MINUS_FILEVERSION "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
configure_file("${SRC}" "${DST}" @ONLY)
