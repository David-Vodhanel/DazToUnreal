set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(DZ_PLUGIN_TGT_NAME dzunrealbridge)
set(DZ_PLUGIN_PROJECT_NAME "DzBridge-Unreal")

include_directories(${COMMON_LIB_INCLUDE_DIR})

# if building a plugin and you want the compiled result placed in the Daz Studio ./plugins directory
if(DAZ_STUDIO_EXE_DIR)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DAZ_STUDIO_EXE_DIR}/plugins)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DAZ_STUDIO_EXE_DIR}/plugins)
endif()

if(WIN32)
	set(OS_SOURCES
		DzUnrealBridge.rc
	)
elseif(APPLE)
	set(OS_SOURCES "")
endif()

if(WIN32)
	set(QRESOURCES "Resources/resources-win.qrc")
elseif(APPLE)
	set(QRESOURCES "Resources/resources-mac.qrc")
endif()

set(DPC_IMAGES_CPP "${CMAKE_CURRENT_BINARY_DIR}/DzUnrealIcon.cpp" )
set(DPC_SRC_IMAGES
	icon.png
)

add_custom_command(OUTPUT ${DPC_IMAGES_CPP}
	COMMAND ${DAZ_SDK_DPC_EXE} -mode image -o ${DPC_IMAGES_CPP} ${DPC_SRC_IMAGES}
	DEPENDS ${DPC_SRC_IMAGES}
	WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

add_library( ${DZ_PLUGIN_TGT_NAME} SHARED
	DzUnrealAction.cpp
	DzUnrealAction.h
	DzUnrealDialog.cpp
	DzUnrealDialog.h
	pluginmain.cpp
	version.h
	${DPC_IMAGES_CPP}
	${OS_SOURCES}
	${QA_SRCS}
	${QRESOURCES}
)

target_include_directories(${DZ_PLUGIN_TGT_NAME}
	PUBLIC
	${FBX_SDK_INCLUDE}
	${OPENSUBDIV_INCLUDE}
	${ALEMBIC_INCLUDE}
	${IMATH_INCLUDE}
)

target_link_libraries(${DZ_PLUGIN_TGT_NAME}
	PRIVATE
	dzcore
	dzbridge-static
	${DZSDK_QT_CORE_TARGET}
	${DZSDK_QT_GUI_TARGET}
	${DZSDK_QT_SCRIPT_TARGET}
	${DZSDK_QT_NETWORK_TARGET}
	${FBX_IMPORT_LIBS}
	${OPENSUBDIV_LIB}
	${ALEMBIC_LIB}
	${IMATH_LIB}
)


set_target_properties(${DZ_PLUGIN_TGT_NAME}
	PROPERTIES
	FOLDER ""
	PROJECT_LABEL ${DZ_PLUGIN_PROJECT_NAME}
)

target_compile_definitions(${DZ_PLUGIN_TGT_NAME}
	PUBLIC
	$<$<CONFIG:DEBUG>:UNITTEST_DZBRIDGE>
	${DZBRIDGE_LIB_FLAGS}
)

set_property(SOURCE ${DPC_IMAGES_CPP} PROPERTY SKIP_AUTOMOC ON)
