
find_package(ZLIB REQUIRED)

# sudo apt install zlib1g zlib1g-dev
create_library(
		NAME
		Npy++
		SOURCES
		dummy.cpp
		PUBLIC_INCLUDE_DIRECTORIES
		. ../MemoryMapping
		SYSTEM_DEPENDENCIES
		ZLIB::ZLIB
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE MATCHES "^(Asan|AsanUbsan)$")
	# Work around GCC 15 -O1 -fsanitize=address false positive -Wnull-dereference inside libstdc++ std::_Hashtable::_M_locate
	target_compile_options(Npy++ INTERFACE -Wno-null-dereference)
endif ()
