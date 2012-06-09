project(lwan C)
cmake_minimum_required(VERSION 2.6)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wshadow")

if (${CMAKE_BUILD_TYPE} MATCHES "Release")
	message(STATUS "Enabling compiler optimizations")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mtune=native -O3 -flto")
endif ()

find_package(Threads)
find_library(TCMALLOC_LIBRARY NAMES tcmalloc /usr/lib /usr/local/lib)
if (TCMALLOC_LIBRARY)
	message(STATUS "TCMalloc found: ${TCMALLOC_LIBRARY}")
else ()
	message(STATUS "TCMalloc not found, using system malloc")
	set(TCMALLOC_LIBRARY "")
endif()

find_path(VALGRIND_INCLUDE_DIR valgrind.h /usr/include /usr/include/valgrind /usr/local/include /usr/local/include/valgrind)
if (VALGRIND_INCLUDE_DIR)
	message(STATUS "Building with Valgrind support")
	add_definitions("-DUSE_VALGRIND=1")
else ()
	message(STATUS "Valgrind headers not found -- disabling valgrind support")
endif()

find_path(LUA_INCLUDE_DIR lua.h
	HINTS $ENV{LUA_DIR}
	PATH_SUFFIXES include/lua52 include/lua5.2 include/lua include
	PATHS /usr/include /usr/local/include
)

find_library(LUA_LIBRARY
	NAMES lua52 lua5.2 lua-5.2 lua
	HINTS $ENV{LUA_DIR}
	PATH_SUFFIXES lib64 lib
	PATHS /usr /usr/local
)

if (LUA_LIBRARY AND LUA_INCLUDE_DIR)
	message(STATUS "Lua 5.2 found at ${LUA_LIBRARY}, includes at ${LUA_INCLUDE_DIR}")
	add_definitions(-DHAS_LUA)
	include_directories(${LUA_INCLUDE_DIR})
	set(LUA_HANDLER "lwan-lua.c")
else()
	message(STATUS "Lua 5.2 not found")
	set(LUA_HANDLER "")
endif ()

add_executable(lwan
	main.c
	lwan.c
	lwan-coro.c
	lwan-trie.c
	lwan-request.c
	lwan-response.c
	lwan-sendfile.c
	lwan-serve-files.c
	int-to-str.c
	lwan-trie.c
	strbuf.c
	${LUA_HANDLER}
)

target_link_libraries(lwan
	${CMAKE_THREAD_LIBS_INIT}
	${TCMALLOC_LIBRARY}
	${LUA_LIBRARY}
)
