CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

AUX_SOURCE_DIRECTORY(./core PIPELINE_SRC)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGL_GLEXT_PROTOTYPES")
SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

FIND_PACKAGE(glm)
IF(glm_FOUND)
	MESSAGE(STATUS      "Looking for glm - found")
ELSE(glm_FOUND)
	MESSAGE(FATAL_ERROR "Looking for glm - not found")
ENDIF(glm_FOUND)


INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/src/Common/include)
INCLUDE_DIRECTORIES(${GLM_INCLUDE_DIRS})

ADD_LIBRARY(glsp_ogl STATIC ${PIPELINE_SRC})

TARGET_LINK_LIBRARIES(glsp_ogl glsp_common)
