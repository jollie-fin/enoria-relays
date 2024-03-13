cmake_minimum_required(VERSION 3.20)

# set the project name
project(enoria-relays)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${PROJECT_SOURCE_DIR}/src/date-submodule/include)

ADD_LIBRARY(LibsModule 
${PROJECT_SOURCE_DIR}/src/db.cpp
${PROJECT_SOURCE_DIR}/src/env.cpp
${PROJECT_SOURCE_DIR}/src/gpio.cpp
${PROJECT_SOURCE_DIR}/src/hwgpio.cpp
${PROJECT_SOURCE_DIR}/src/ics.cpp
${PROJECT_SOURCE_DIR}/src/sqlite.cpp
${PROJECT_SOURCE_DIR}/src/usbrelay.cpp
${PROJECT_SOURCE_DIR}/src/utils.cpp
${PROJECT_SOURCE_DIR}/src/frisquetconnect.cpp
${PROJECT_SOURCE_DIR}/src/date-submodule/src/tz.cpp
)
target_link_libraries(LibsModule -lcurl)
target_link_libraries(LibsModule -lsqlite3)
target_link_libraries(LibsModule -lhidapi-hidraw)

# add the executable
add_executable(
    enoria-relays
    ${PROJECT_SOURCE_DIR}/src/main.cpp
    ${PROJECT_SOURCE_DIR}/data/gpio.cfg
    ${PROJECT_SOURCE_DIR}/data/events.db
    ${PROJECT_SOURCE_DIR}/data/env
    )

target_link_libraries(enoria-relays LibsModule)

include(CheckIncludeFileCXX)
check_include_file_cxx("format" CXX_FORMAT_SUPPORT)
if (NOT CXX_FORMAT_SUPPORT)
    find_package(fmt REQUIRED)
endif()

if (NOT CXX_FORMAT_SUPPORT)
    target_link_libraries(${PROJECT_NAME} fmt::fmt)
endif()


install(TARGETS enoria-relays DESTINATION bin)
# install(FILES data/gpio.cfg DESTINATION etc/enoria-relays COMPONENT config)
# install(FILES data/env DESTINATION etc/enoria-relays COMPONENT config)
install(DIRECTORY DESTINATION var/enoria-relays
        DIRECTORY_PERMISSIONS
            OWNER_WRITE OWNER_READ OWNER_EXECUTE
            GROUP_WRITE GROUP_READ GROUP_EXECUTE
            WORLD_WRITE WORLD_READ WORLD_EXECUTE
        COMPONENT database)
# install(FILES data/events.db
#         DESTINATION var/enoria-relays
#         PERMISSIONS
#             OWNER_WRITE OWNER_READ
#             GROUP_WRITE GROUP_READ
#             WORLD_WRITE WORLD_READ
#         COMPONENT database)

