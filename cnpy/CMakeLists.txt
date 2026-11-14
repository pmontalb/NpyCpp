
create_library(
		NAME
		cnpy
		SOURCES
		../cnpy/cnpy.cpp
		PUBLIC_INCLUDE_DIRECTORIES
		../
		SYSTEM_DEPENDENCIES
		z
		DO_NOT_USE_WARNINGS
		DO_NOT_USE_PEDANTIC_WARNINGS
)

if (NOT MSVC)
	target_compile_options(cnpy PUBLIC -frtti -fexceptions)
endif ()
