cmake_minimum_required(VERSION 3.0.0)

#PROJECT_SOURCE_DIR	--> dir of most recent project() call
#CMAKE_SOURCE_DIR --> top level CMakeLists.txt
#CMAKE_CURRENT_SOURCE_DIR --> current CMakeLists.txt (add_subdirectory() call) should be similar to PROJECT_SOURCE_DIR

project(miniscope)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-pg ) #-Wall -Wextra

set(SOURCE_LIST
	"miniscope.c"
)

#defines the target:
add_executable(${PROJECT_NAME} ${SOURCE_LIST})

set(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

target_include_directories(${PROJECT_NAME}
	PRIVATE
)
