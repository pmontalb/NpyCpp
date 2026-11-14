
find_package(ZLIB REQUIRED)

create_library(
		NAME
		cnpy
		SOURCES
		../cnpy/cnpy.cpp
		PUBLIC_INCLUDE_DIRECTORIES
		../
		SYSTEM_DEPENDENCIES
		ZLIB::ZLIB
		DO_NOT_USE_WARNINGS
		DO_NOT_USE_PEDANTIC_WARNINGS
)

if (MSVC)
	target_compile_options(cnpy PUBLIC /GR /EHsc)
else ()
	target_compile_options(cnpy PUBLIC -frtti -fexceptions)
endif ()
