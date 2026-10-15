# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the DJV project.

# The .env file at the top of the source tree holds what a studio keeps to
# itself -- the address of its sync server, the name of its administrator --
# so that none of it is written in the code. git ignores it; .env.example,
# which is committed, has the same keys with example values, and is what a
# build reads when there is no .env, a public build among them.
#
# djv_read_dotenv(<key> <out_var>) sets <out_var> to the key's value in .env,
# or else in .env.example, and to "" when neither has it. The line format is
# the plain KEY=value that Docker compose and Node read too: no export, and
# quotes around the value are taken off. Both files are configure
# dependencies, so an edit to either takes effect on the next build.
function(djv_read_dotenv key out_var)
    set(value "")
    set(found FALSE)
    set(${out_var}_FROM "neither .env nor .env.example" PARENT_SCOPE)
    foreach(file ${PROJECT_SOURCE_DIR}/.env ${PROJECT_SOURCE_DIR}/.env.example)
        if(EXISTS ${file})
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
            if(NOT found)
                file(STRINGS ${file} lines REGEX "^[ \t]*${key}[ \t]*=")
                if(lines)
                    # The last one wins, as it does for Docker compose.
                    list(GET lines -1 line)
                    string(REGEX REPLACE "^[ \t]*${key}[ \t]*=[ \t]*" "" value "${line}")
                    string(STRIP "${value}" value)
                    string(REGEX REPLACE "^\"(.*)\"$" "\\1" value "${value}")
                    string(REGEX REPLACE "^'(.*)'$" "\\1" value "${value}")
                    set(found TRUE)
                    file(RELATIVE_PATH from ${PROJECT_SOURCE_DIR} ${file})
                    set(${out_var}_FROM ${from} PARENT_SCOPE)
                endif()
            endif()
        endif()
    endforeach()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()
